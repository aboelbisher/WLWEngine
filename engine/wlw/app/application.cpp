#include "wlw/app/application.h"

#include "wlw/core/log.h"
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

Application::~Application() = default;

int Application::Run() {
  // Engine start happens here, not in the constructor: virtual hooks can't be called from a
  // base-class constructor, and the derived game's members must exist before OnStart().
  if (!engine_->Start(window_)) {
    WLW_LOG_ERROR("Engine failed to start");
    return 1;
  }
#if WLW_DEBUG_UI
  debug_ui_ = std::make_unique<debug::DebugUI>(*window_);  // needs the GL context Start() made
#endif
  OnStart();

  // steady_clock: monotonic (never jumps when the system clock changes), available on every platform.
  using Clock = std::chrono::steady_clock;
  Clock::time_point previous = Clock::now();
  const Clock::time_point start = previous;

  while (!quit_requested_ && !window_->ShouldClose()) {
    if (window_->ConsumeResumed()) {
      // Back from the background (mobile): don't count the time we were away as one huge frame.
      previous = Clock::now();
      timestep_.Reset();
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

    const int steps = timestep_.Advance(frame_seconds);
    for (int i = 0; i < steps; ++i) {
      OnFixedUpdate(timestep_.Step());
    }

    OnUpdate(frame_dt);
    OnRender(timestep_.Alpha());

#if WLW_DEBUG_UI
    if (debug_ui_->IsOpen()) {
      DrawEngineDebugWindow();
      OnDebugUI();
    }
    debug_ui_->EndFrame();  // drawn on top of the scene by Iterate()
#endif

    engine_->Iterate();  // draw + present every window, then poll input
    UpdateStats(frame_seconds);
  }

  OnShutdown();
  return 0;
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
