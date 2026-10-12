#include "wlw/app/application.h"

#include "wlw/core/log.h"
#include "wlw/audio/audio.h"
#include "wlw/platform/bridge.h"
#include "wlw/platform/platform.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/ui_context.h"
#if WLW_DEBUG_UI
#include "wlw/debug/debug_ui.h"
#include "wlw/debug/engine_panel.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace wlw::app {

Application::Application(AppConfig config)
    : config_(std::move(config)),
      engine_(WLWEngine::Create()),
      window_(scene::Window::Create(config_.window_size, config_.title)),
      timestep_(config_.fixed_timestep, config_.max_frame_time) {
  time_.fixed_step_ = config_.fixed_timestep;
#if WLW_DEBUG_UI
  AddSystem<debug::EnginePanel>();  // the engine's own debug window, first of the debug panels
#endif
}

Application::~Application() = default;

int Application::Run() {
  if (!engine_->Start(window_)) {
    WLW_LOG_ERROR("Engine failed to start");
    return 1;
  }
  audio_ = std::make_unique<audio::Audio>();
  ui_ = std::make_unique<ui::UIContext>(window_->GetScreen().GetCanvas());
#if WLW_DEBUG_UI
  debug_ui_ = std::make_unique<debug::DebugUI>(*window_);  // needs the GL context Start() made
#endif
  systems_.StartPending();  // the systems added before Run, in order

  // steady_clock: monotonic (never jumps when the system clock changes), available on every platform.
  using Clock = std::chrono::steady_clock;
  Clock::time_point previous = Clock::now();
  const Clock::time_point start = previous;

  while (!quit_requested_ && !window_->ShouldClose()) {
    systems_.StartPending();  // systems added during the last frame start now

    // 1. Lifecycle. Paused before resumed: coming back from the background, both can arrive in
    // one frame (the loop doesn't run while the app is away), and the game must end up paused
    // but audible.
    if (window_->ConsumePaused()) {
      audio_->Suspend();  // silent in the background
      systems_.ForEach([](System& system) { system.OnAppPaused(); });
      screens_.AppPaused();  // e.g. the game screen opens its pause panel
    }
    if (window_->ConsumeResumed()) {
      // Back from the background (mobile): don't count the time we were away as one huge frame.
      previous = Clock::now();
      timestep_.Reset();
      audio_->Resume();
      systems_.ForEach([](System& system) { system.OnAppResumed(); });
    }

    // 2. Time.
    const Clock::time_point now = Clock::now();
    const double frame_seconds = std::chrono::duration<double>(now - previous).count();
    previous = now;
    time_.since_start_ = std::chrono::duration<double>(now - start).count();
    // Clamp like the stepper does, so a breakpoint doesn't hand OnUpdate a 30-second dt.
    time_.real_delta_ = static_cast<float>(std::min(frame_seconds, static_cast<double>(config_.max_frame_time)));
    // Game time: real time through the time scale (hit-stop, slow motion). Once per frame.
    time_.delta_ = time_.scale_.Apply(time_.real_delta_);
    const int steps = timestep_.Advance(time_.delta_);
    time_.alpha_ = timestep_.Alpha();

#if WLW_DEBUG_UI
    // First, so the debug UI can claim the pointer before the game reads input this frame.
    debug_ui_->BeginFrame(time_.real_delta_);
#endif

    // 3. Back: the top screen first (close a panel...), then the systems; with nothing left to
    // close, leave like any Android app does (desktop: Escape quits).
    const input::Input& input = window_->GetInput();
    if (input.WasKeyPressed(input::Key::Back) || input.WasKeyPressed(input::Key::Escape)) {
      if (!screens_.Back() && !systems_.Back() && !platform::MoveToBackground()) Quit();
    }

    // 4. Results from platform code (an ad closed, a cloud save arrived...) reach the game here,
    // on its own thread, before this frame's updates.
    platform::DispatchBridgeMessages();

    // 5-7. Updates and rendering: the systems, then the screens.
    const float step = timestep_.Step();
    for (int i = 0; i < steps; ++i) {
      systems_.ForEach([step](System& system) { system.OnFixedUpdate(step); });
      screens_.FixedUpdate(step);
    }
    const float dt = time_.delta_;
    systems_.ForEach([dt](System& system) { system.OnUpdate(dt); });
    screens_.Update(dt);
    const float alpha = time_.alpha_;
    systems_.ForEach([alpha](System& system) { system.OnRender(alpha); });
    screens_.Render(alpha);

    // 8. UI.
    BuildUI();

    // 9. Debug panels.
#if WLW_DEBUG_UI
    if (debug_ui_->IsOpen()) {
      systems_.ForEach([](System& system) { system.OnDebugUI(); });
      screens_.DebugUI();
    }
    debug_ui_->EndFrame();  // drawn on top of the scene by Iterate()
#endif

    // 10. Draw + present every window, then poll input; screen changes requested this frame
    // happen now, between frames.
    engine_->Iterate();
    screens_.EndFrame(time_.real_delta_);
    UpdateStats(frame_seconds);
  }

  screens_.Clear();  // OnExit for every screen while the systems are still fully alive
  systems_.ForEach([](System& system) { system.OnShutdown(); });
  return 0;
}

void Application::BuildUI() {
  const core::Vector2 framebuffer = window_->GetSize();
  const glm::vec2 reference{config_.ui_reference_size.x, config_.ui_reference_size.y};
  // Offscreen viewports' canvases: emptied and sized to their viewport (same units as the UI's
  // when proportioned like the screen); the game draws into them during OnRenderUI.
  for (const auto& [_, viewport] : window_->GetViewports()) {
    const glm::ivec2 pixels = viewport->PixelSize({framebuffer.x, framebuffer.y});
    viewport->GetCanvas().BeginFrame(glm::vec2(pixels), reference);
  }

  ui::Canvas& canvas = ui_->canvas;  // the game UI: the screen's canvas, over its scene
  canvas.BeginFrame({framebuffer.x, framebuffer.y}, reference);
  ui_->safe_area = ui::SafeAreaFor(canvas, platform::GetSafeAreaInsets());

  // The pointer for UI widgets, in Canvas units: pointer units -> framebuffer pixels -> units.
  // Uses the normal (blockable) queries, so the debug UI still takes precedence.
  const input::Input& input = window_->GetInput();
  const core::Vector2 surface = input.SurfaceSize();
  ui::PointerState pointer;
  if (surface.x > 0.0f && surface.y > 0.0f) {
    const core::Vector2 p = input.PointerPosition();
    pointer.position = canvas.FromPixels({p.x * framebuffer.x / surface.x, p.y * framebuffer.y / surface.y});
  }
  pointer.down = input.IsPointerDown();
  pointer.pressed = input.WasPointerPressed();
  pointer.released = input.WasPointerReleased();
  ui_->pointer = pointer;

  ui::UIContext& ui = *ui_;
  systems_.ForEach([&ui](System& system) { system.OnRenderUI(ui); });
  screens_.RenderUI(ui);
  if (const float fade = screens_.FadeAmount(); fade > 0.0f) {  // screen transitions
    canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, {0.0f, 0.0f, 0.0f, fade});
  }
}

void Application::UpdateStats(double frame_seconds) {
  ++stats_frames_;
  stats_seconds_ += frame_seconds;
  if (stats_seconds_ < 0.5) return;
  time_.fps_ = static_cast<float>(stats_frames_ / stats_seconds_);
  time_.frame_ms_ = static_cast<float>(1000.0 * stats_seconds_ / stats_frames_);
  stats_frames_ = 0;
  stats_seconds_ = 0.0;

  if (config_.show_stats_in_title) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), " | %.0f fps | %.1f ms", time_.fps_, time_.frame_ms_);
    window_->SetTitle(config_.title + buffer);
  }
}

}  // namespace wlw::app
