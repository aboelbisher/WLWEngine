#include "wlw/app/application.h"

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
  engine_->Start(window_);
  OnStart();

  // steady_clock: monotonic (never jumps when the system clock changes), available on every platform.
  using Clock = std::chrono::steady_clock;
  Clock::time_point previous = Clock::now();
  const Clock::time_point start = previous;

  while (!quit_requested_ && !window_->ShouldClose()) {
    const Clock::time_point now = Clock::now();
    const double frame_seconds = std::chrono::duration<double>(now - previous).count();
    previous = now;
    time_since_start_ = std::chrono::duration<double>(now - start).count();

    const int steps = timestep_.Advance(frame_seconds);
    for (int i = 0; i < steps; ++i) {
      OnFixedUpdate(timestep_.Step());
    }

    // Clamp like the stepper does, so a breakpoint doesn't hand OnUpdate a 30-second dt.
    const float frame_dt = static_cast<float>(std::min(frame_seconds, static_cast<double>(config_.max_frame_time)));
    OnUpdate(frame_dt);
    OnRender(timestep_.Alpha());

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
