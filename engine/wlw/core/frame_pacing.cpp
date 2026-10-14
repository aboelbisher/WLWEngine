#include "wlw/core/frame_pacing.h"

#include <cmath>

namespace wlw::core {

std::string TargetFpsName(int target_fps) {
  if (target_fps == kTargetDisplayRate) return "display";
  if (target_fps < 0) return "uncapped";
  return std::to_string(target_fps);
}

PacingPlan PlanPacing(int target_fps, double refresh_hz) {
  if (target_fps < 0) return {.swap_interval = 0, .limit_seconds = 0.0, .fps = 0.0};
  const double refresh = refresh_hz > 0.0 ? refresh_hz : 60.0;
  const double target = static_cast<double>(target_fps);
  // At or above the refresh rate (within 2%: displays report 59.94 Hz as 59 or 60): one frame per refresh.
  if (target_fps == kTargetDisplayRate || target >= refresh * 0.98) {
    return {.swap_interval = 1, .limit_seconds = 0.0, .fps = refresh};
  }
  const double ratio = refresh / target;
  const double interval = std::round(ratio);
  if (std::abs(refresh / interval - target) <= target * 0.02) {
    return {.swap_interval = static_cast<int>(interval), .limit_seconds = 0.0, .fps = refresh / interval};
  }
  return {.swap_interval = 0, .limit_seconds = 1.0 / target, .fps = target};
}

std::string PacingPlanName(const PacingPlan& plan) {
  if (plan.swap_interval == 1) return "vsync";
  if (plan.swap_interval > 1) return "vsync / " + std::to_string(plan.swap_interval);
  return plan.limit_seconds > 0.0 ? "limiter" : "none";
}

void FrameLimiter::SetPeriod(double seconds) {
  const double period = seconds > 0.0 ? seconds : 0.0;
  if (period == period_) return;
  period_ = period;
  started_ = false;
}

double FrameLimiter::Next(double now) {
  if (period_ <= 0.0) return 0.0;
  if (!started_) {  // the first frame of a schedule: it starts now
    started_ = true;
    next_ = now;
    return 0.0;
  }
  next_ += period_;
  if (now > next_ + period_) {  // more than a whole frame behind: restart rather than rush to catch up
    next_ = now;
    return 0.0;
  }
  return now < next_ ? next_ - now : 0.0;
}

}  // namespace wlw::core
