// Tests for the frame's time math (app/frame_clock.h): measuring, clamping, the time scale, fixed
// steps and alpha, time since start, and the restart after the app comes back from the background.
#include "test_helpers.h"
#include "wlw/app/frame_clock.h"

TEST_SUITE_BEGIN("frame_clock");

using namespace wlw;
using app::FrameClock;
using app::Time;

TEST_CASE("frame clock: steps, alpha and time since start") {
  FrameClock clock(0.01f, 0.25f);
  Time time;
  clock.Start(100.0);

  FrameClock::Tick tick = clock.Advance(100.025, time);  // 2.5 steps
  CHECK(tick.steps == 2);
  CHECK(Near(static_cast<float>(tick.seconds), 0.025f));
  CHECK((Near(time.Delta(), 0.025f) && Near(time.RealDelta(), 0.025f)));
  CHECK(Near(time.Alpha(), 0.5f));
  CHECK(Near(time.FixedStep(), 0.01f));
  CHECK(Near(static_cast<float>(time.SinceStart()), 0.025f));

  tick = clock.Advance(100.0325, time);  // the half step left + 0.75 = 1.25 steps
  CHECK(tick.steps == 1);
  CHECK(Near(time.Alpha(), 0.25f));
  CHECK(Near(static_cast<float>(time.SinceStart()), 0.0325f));
}

TEST_CASE("frame clock: a long frame is clamped, but measured in full") {
  FrameClock clock(0.01f, 0.25f);
  Time time;
  clock.Start(0.0);
  const FrameClock::Tick tick = clock.Advance(30.0, time);  // a breakpoint
  CHECK(Near(static_cast<float>(tick.seconds), 30.0f));     // stats and quality see what happened
  CHECK((Near(time.RealDelta(), 0.25f) && Near(time.Delta(), 0.25f)));
  CHECK(tick.steps == 25);
  CHECK(Near(static_cast<float>(time.SinceStart()), 30.0f));
}

TEST_CASE("frame clock: the time scale slows game time only") {
  FrameClock clock(0.01f, 0.25f);
  Time time;
  time.SetScale(0.5f);
  clock.Start(0.0);
  const FrameClock::Tick tick = clock.Advance(0.04, time);
  CHECK((Near(time.RealDelta(), 0.04f) && Near(time.Delta(), 0.02f)));
  CHECK(tick.steps == 2);  // fixed steps follow game time

  time.SetScale(0.0f);  // paused: no steps, alpha stays
  const float alpha = time.Alpha();
  CHECK(clock.Advance(0.1, time).steps == 0);
  CHECK((Near(time.Delta(), 0.0f) && Near(time.RealDelta(), 0.06f) && Near(time.Alpha(), alpha)));
}

TEST_CASE("frame clock: a restart drops the time away and the leftover step") {
  FrameClock clock(0.01f, 0.25f);
  Time time;
  clock.Start(0.0);
  clock.Advance(0.015, time);  // half a step left over
  CHECK(Near(time.Alpha(), 0.5f));

  clock.Restart(600.0);  // ten minutes in the background
  const FrameClock::Tick tick = clock.Advance(600.004, time);
  CHECK(Near(static_cast<float>(tick.seconds), 0.004f));
  CHECK(tick.steps == 0);  // 0.004 alone: the old half step is gone
  CHECK(Near(time.Alpha(), 0.4f));
  CHECK(Near(static_cast<float>(time.SinceStart()), 600.004f, 1e-3f));  // real time since start goes on
}

TEST_CASE("frame clock: a clock going backwards counts as a zero-length frame") {
  FrameClock clock(0.01f, 0.25f);
  Time time;
  clock.Start(5.0);
  CHECK(clock.Advance(4.0, time).steps == 0);
  CHECK((Near(time.RealDelta(), 0.0f) && Near(time.Delta(), 0.0f)));
}

TEST_SUITE_END();
