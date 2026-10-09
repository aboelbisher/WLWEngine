#include "wlw/app/application.h"

#include "wlw/core/log.h"
#include "wlw/audio/audio.h"
#include "wlw/platform/platform.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/ui_renderer.h"
#if WLW_DEBUG_UI
#include "imgui.h"
#include "wlw/debug/debug_ui.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace wlw {

Application::Application(AppConfig config)
    : config_(std::move(config)),
      engine_(WLWEngine::Create()),
      window_(scene::Window::Create(config_.window_size, config_.title)),
      timestep_(config_.fixed_timestep, config_.max_frame_time) {}

Application::~Application() {
  if (ui_overlay_id_ >= 0) window_->RemoveOverlay(ui_overlay_id_);
}

int Application::Run() {
  // Engine start happens here, not in the constructor: virtual hooks can't be called from a
  // base-class constructor, and the derived game's members must exist before OnStart().
  if (!engine_->Start(window_)) {
    WLW_LOG_ERROR("Engine failed to start");
    return 1;
  }
  audio_ = std::make_unique<audio::Audio>();
  // The game UI layer, drawn over the 3D scene (and under the debug UI, created next).
  canvas_ = std::make_unique<ui::Canvas>();
  ui_renderer_ = ui::UIRenderer::Create();
  ui_overlay_id_ = window_->AddOverlay([this] {
    const core::Vector2 framebuffer = window_->GetSize();
    ui_renderer_->Draw(*canvas_, {framebuffer.x, framebuffer.y});
  });
#if WLW_DEBUG_UI
  debug_ui_ = std::make_unique<debug::DebugUI>(*window_);  // needs the GL context Start() made
#endif
  OnStart();

  // steady_clock: monotonic (never jumps when the system clock changes), available on every platform.
  using Clock = std::chrono::steady_clock;
  Clock::time_point previous = Clock::now();
  const Clock::time_point start = previous;

  while (!quit_requested_ && !window_->ShouldClose()) {
    // Paused before resumed: coming back from the background, both can arrive in one frame (the
    // loop doesn't run while the app is away), and the game must end up paused but audible.
    if (window_->ConsumePaused()) {
      audio_->Suspend();  // silent in the background
      OnAppPaused();
      screens_.AppPaused();  // e.g. the game screen opens its pause panel
    }
    if (window_->ConsumeResumed()) {
      // Back from the background (mobile): don't count the time we were away as one huge frame.
      previous = Clock::now();
      timestep_.Reset();
      audio_->Resume();
    }
    const Clock::time_point now = Clock::now();
    const double frame_seconds = std::chrono::duration<double>(now - previous).count();
    previous = now;
    time_since_start_ = std::chrono::duration<double>(now - start).count();
    // Clamp like the stepper does, so a breakpoint doesn't hand OnUpdate a 30-second dt.
    const float frame_dt = static_cast<float>(std::min(frame_seconds, static_cast<double>(config_.max_frame_time)));

#if WLW_DEBUG_UI
    // First, so the debug UI can claim the pointer before the game reads input this frame.
    debug_ui_->BeginFrame(frame_dt);
#endif

    // Back: the top screen first (close a panel...), then the game; with nothing left to close,
    // leave like any Android app does (desktop: Escape quits).
    const input::Input& input = window_->GetInput();
    if (input.WasKeyPressed(input::Key::Back) || input.WasKeyPressed(input::Key::Escape)) {
      if (!screens_.Back() && !OnBack() && !platform::MoveToBackground()) Quit();
    }

    const int steps = timestep_.Advance(frame_seconds);
    for (int i = 0; i < steps; ++i) {
      OnFixedUpdate(timestep_.Step());
      screens_.FixedUpdate(timestep_.Step());
    }

    OnUpdate(frame_dt);
    screens_.Update(frame_dt);
    OnRender(timestep_.Alpha());
    screens_.Render(timestep_.Alpha());

    BuildUI();

#if WLW_DEBUG_UI
    if (debug_ui_->IsOpen()) {
      DrawEngineDebugWindow();
      OnDebugUI();
      screens_.DebugUI();
    }
    debug_ui_->EndFrame();  // drawn on top of the scene by Iterate()
#endif

    engine_->Iterate();  // draw + present every window, then poll input
    screens_.EndFrame(frame_dt);  // screen changes requested this frame happen now, between frames
    UpdateStats(frame_seconds);
  }

  screens_.Clear();  // OnExit for every screen while the game object is still fully alive
  OnShutdown();
  return 0;
}

void Application::BuildUI() {
  const core::Vector2 framebuffer = window_->GetSize();
  canvas_->BeginFrame({framebuffer.x, framebuffer.y}, platform::GetSafeAreaInsets(),
                      {config_.ui_reference_size.x, config_.ui_reference_size.y});

  // The pointer for UI widgets, in Canvas units: pointer units -> framebuffer pixels -> units.
  // Uses the normal (blockable) queries, so the debug UI still takes precedence.
  const input::Input& input = window_->GetInput();
  const core::Vector2 surface = input.SurfaceSize();
  ui::PointerState pointer;
  if (surface.x > 0.0f && surface.y > 0.0f) {
    const core::Vector2 p = input.PointerPosition();
    pointer.position = canvas_->FromPixels({p.x * framebuffer.x / surface.x, p.y * framebuffer.y / surface.y});
  }
  pointer.down = input.IsPointerDown();
  pointer.pressed = input.WasPointerPressed();
  pointer.released = input.WasPointerReleased();
  canvas_->SetPointer(pointer);

  OnRenderUI(*canvas_);
  screens_.RenderUI(*canvas_);
  if (const float fade = screens_.FadeAmount(); fade > 0.0f) {  // screen transitions
    canvas_->FillRect({{0.0f, 0.0f}, canvas_->Size()}, {0.0f, 0.0f, 0.0f, fade});
  }
}

void Application::UpdateStats(double frame_seconds) {
  ++stats_frames_;
  stats_seconds_ += frame_seconds;
  if (stats_seconds_ < 0.5) {
    return;
  }
  fps_ = static_cast<float>(stats_frames_ / stats_seconds_);
  frame_ms_ = static_cast<float>(1000.0 * stats_seconds_ / stats_frames_);
  stats_frames_ = 0;
  stats_seconds_ = 0.0;

  if (config_.show_stats_in_title) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), " | %.0f fps | %.1f ms", fps_, frame_ms_);
    window_->SetTitle(config_.title + buffer);
  }
}

}  // namespace wlw

#if WLW_DEBUG_UI
namespace wlw {

void Application::DrawEngineDebugWindow() {
  ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Engine");
  ImGui::Text("%.0f fps, %.2f ms", fps_, frame_ms_);
  const scene::Window::RenderStats& stats = window_->GetRenderStats();
  ImGui::Text("%d draw calls, %d instances, %d culled", stats.draw_calls, stats.instances, stats.culled_nodes);
  const core::Vector2 framebuffer = window_->GetSize();
  const core::Vector2 surface = window_->GetInput().SurfaceSize();
  ImGui::Text("Framebuffer %.0f x %.0f px", framebuffer.x, framebuffer.y);
  ImGui::Text("Pointer area %.0f x %.0f%s", surface.x, surface.y, window_->GetInput().IsTouchScreen() ? " (touch)" : "");
  ImGui::Checkbox("ImGui demo window", &show_imgui_demo_);
  ImGui::End();
  if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);  // every widget, with code
}

}  // namespace wlw
#endif
