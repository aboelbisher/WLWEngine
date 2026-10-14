// Tests for frame pacing (core/frame_pacing.h): how a target becomes a vsync interval or the limiter
// on a given display, and the limiter's schedule (exact average, catching up, restarting after a stall).
#include <cmath>
#include <string>

#include "test_helpers.h"
#include "wlw/core/frame_pacing.h"

TEST_SUITE_BEGIN("frame_pacing");

using namespace wlw;
using core::FrameLimiter;
using core::PacingPlan;
using core::PlanPacing;

namespace {
bool NearD(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }  // times are doubles
}  // namespace

TEST_CASE("target names") {
  CHECK(core::TargetFpsName(60) == "60");
  CHECK(core::TargetFpsName(core::kTargetDisplayRate) == "display");
  CHECK(core::TargetFpsName(core::kTargetUncapped) == "uncapped");
}

TEST_CASE("display rate and targets at or above it: vsync") {
  CHECK(PlanPacing(core::kTargetDisplayRate, 144.0) ==
        PacingPlan{.swap_interval = 1, .limit_seconds = 0.0, .fps = 144.0});
  CHECK(PlanPacing(60, 60.0) == PacingPlan{.swap_interval = 1, .limit_seconds = 0.0, .fps = 60.0});
  CHECK(PlanPacing(120, 60.0) == PacingPlan{.swap_interval = 1, .limit_seconds = 0.0, .fps = 60.0});  // can't show more
  CHECK(PlanPacing(60, 59.0).swap_interval == 1);  // displays report 59.94 Hz as 59
  CHECK(PlanPacing(60, 0.0) == PacingPlan{.swap_interval = 1, .limit_seconds = 0.0, .fps = 60.0});  // unknown = 60 Hz
}

TEST_CASE("whole fractions of the refresh: a vsync interval") {
  CHECK(PlanPacing(30, 60.0) == PacingPlan{.swap_interval = 2, .limit_seconds = 0.0, .fps = 30.0});
  CHECK(PlanPacing(72, 144.0) == PacingPlan{.swap_interval = 2, .limit_seconds = 0.0, .fps = 72.0});
  CHECK(PlanPacing(30, 120.0) == PacingPlan{.swap_interval = 4, .limit_seconds = 0.0, .fps = 30.0});
  CHECK(PlanPacing(60, 120.0).swap_interval == 2);
  const PacingPlan ntsc = PlanPacing(30, 59.0);  // 29.5 fps: close enough to 30, and even
  CHECK((ntsc.swap_interval == 2 && NearD(ntsc.fps, 29.5, 1e-9)));
}

TEST_CASE("other targets: vsync off and the limiter") {
  CHECK(PlanPacing(60, 144.0) == PacingPlan{.swap_interval = 0, .limit_seconds = 1.0 / 60.0, .fps = 60.0});
  CHECK(PlanPacing(50, 60.0) == PacingPlan{.swap_interval = 0, .limit_seconds = 1.0 / 50.0, .fps = 50.0});
  CHECK(PlanPacing(90, 144.0).limit_seconds > 0.0);
}

TEST_CASE("uncapped: nothing") {
  CHECK(PlanPacing(core::kTargetUncapped, 60.0) == PacingPlan{.swap_interval = 0, .limit_seconds = 0.0, .fps = 0.0});
}

TEST_CASE("plan names") {
  CHECK(core::PacingPlanName(PlanPacing(60, 60.0)) == "vsync");
  CHECK(core::PacingPlanName(PlanPacing(30, 60.0)) == "vsync / 2");
  CHECK(core::PacingPlanName(PlanPacing(60, 144.0)) == "limiter");
  CHECK(core::PacingPlanName(PlanPacing(core::kTargetUncapped, 60.0)) == "none");
}

TEST_CASE("limiter off: never waits") {
  FrameLimiter limiter;
  CHECK(limiter.Next(1.0) == 0.0);
  limiter.SetPeriod(0.0);
  CHECK(limiter.Next(2.0) == 0.0);
}

TEST_CASE("limiter: frames on a fixed schedule") {
  FrameLimiter limiter;
  limiter.SetPeriod(0.02);
  CHECK(limiter.Next(10.0) == 0.0);                 // the first frame starts the schedule
  CHECK(NearD(limiter.Next(10.005), 0.015, 1e-9));  // a 5 ms frame waits until 10.02
  CHECK(NearD(limiter.Next(10.02), 0.02, 1e-9));    // woke on time: the next starts at 10.04
}

TEST_CASE("limiter: a late frame is followed by a shorter one (exact average)") {
  FrameLimiter limiter;
  limiter.SetPeriod(0.02);
  limiter.Next(0.0);
  CHECK(limiter.Next(0.03) == 0.0);                // due at 0.02, ended at 0.03: no wait
  CHECK(NearD(limiter.Next(0.035), 0.005, 1e-9));  // the next is still due at 0.04
  // 100 frames of 5 ms work take 100 periods.
  double now = 0.04;
  for (int i = 0; i < 100; ++i) now += 0.005 + limiter.Next(now + 0.005);
  CHECK(NearD(now, 0.04 + 100 * 0.02, 1e-9));
}

TEST_CASE("limiter: a stall restarts the schedule instead of rushing") {
  FrameLimiter limiter;
  limiter.SetPeriod(0.02);
  limiter.Next(0.0);
  CHECK(limiter.Next(1.0) == 0.0);                 // a 1 s hitch (a breakpoint, a load)
  CHECK(NearD(limiter.Next(1.005), 0.015, 1e-9));  // then a full period from the hitch's end, not 49 short frames
}

TEST_CASE("limiter: a new period or a reset starts a new schedule") {
  FrameLimiter limiter;
  limiter.SetPeriod(0.02);
  limiter.Next(0.0);
  limiter.SetPeriod(0.05);
  CHECK(limiter.Period() == 0.05);
  CHECK(limiter.Next(0.001) == 0.0);
  CHECK(NearD(limiter.Next(0.011), 0.04, 1e-9));
  limiter.Reset();
  CHECK(limiter.Next(5.0) == 0.0);
}

TEST_SUITE_END();
