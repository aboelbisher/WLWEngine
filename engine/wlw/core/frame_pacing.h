#pragma once

#include <string>

// Frame pacing: a frame-rate target (AppConfig::target_fps, Application::SetTargetFps) turned into
// how frames are presented, like Unity's Application.targetFrameRate or Unreal's t.MaxFPS.
//
// Android hands the target to Swappy (platform/android), which times each frame to the display's
// vsync and picks the display mode. Elsewhere (desktop, and phones where Swappy isn't available)
// PlanPacing chooses between a vsync interval (30 fps on a 60 Hz screen = present every 2nd
// refresh) and a FrameLimiter (60 fps on a 144 Hz screen: vsync off, a precise wait per frame).
//
// Both are plain logic fed with times, tested without a window; the platform does the waiting
// (platform::PreciseSleep).
namespace wlw::core {

inline constexpr int kTargetDisplayRate = 0;  // target_fps: as fast as the display refreshes (vsync)
inline constexpr int kTargetUncapped = -1;    // target_fps: no vsync, no limit (desktop profiling)

// A target to a readable string: "60", "display", "uncapped".
std::string TargetFpsName(int target_fps);

struct PacingPlan {
  int swap_interval = 1;       // 0 = vsync off; N = present on every Nth refresh
  double limit_seconds = 0.0;  // > 0: a FrameLimiter keeps frames at least this long
  double fps = 0.0;            // the rate this plan gives (0: uncapped)
  bool operator==(const PacingPlan&) const = default;
};

// How to reach `target_fps` on a display refreshing at `refresh_hz` (<= 0: unknown, taken as 60):
//   - kTargetUncapped: vsync off, no limiter;
//   - display rate, or a target at or above it (more frames than the screen shows are wasted): vsync;
//   - a whole fraction of the refresh (30 on 60 Hz, 72 on 144 Hz; within 2%): vsync every Nth refresh;
//   - anything else (60 on 144 Hz, 50 on 60 Hz): vsync off + the limiter (vsync would round each
//     frame to the next refresh, uneven).
PacingPlan PlanPacing(int target_fps, double refresh_hz);
// "vsync", "vsync / 2", "limiter" or "none" (for the engine panel and logs).
std::string PacingPlanName(const PacingPlan& plan);

// Keeps frames at least `period` seconds apart on a fixed schedule: deadlines advance by exactly one
// period, so the average rate is exact (a frame that ends late is followed by a shorter wait). After
// a stall longer than a whole period (a breakpoint, a slow load) the schedule restarts from now instead
// of rushing frames to catch up.
//
//   double wait = limiter.Next(now);   // right after presenting
//   if (wait > 0) platform::PreciseSleep(wait);
class FrameLimiter {
 public:
  void SetPeriod(double seconds);  // 0 = off; a change restarts the schedule
  double Period() const { return period_; }
  void Reset() { started_ = false; }  // the next frame starts a new schedule (after a pause)

  // `now` in seconds (any monotonic clock). Returns how long to wait before the next frame starts.
  double Next(double now);

 private:
  double period_ = 0.0;
  double next_ = 0.0;  // when the next frame should start
  bool started_ = false;
};

}  // namespace wlw::core
