#pragma once

#include "wlw/app/time.h"
#include "wlw/core/fixed_timestep.h"

namespace wlw::app {

// Phase 2 of the frame (application.h): turns clock readings into this frame's Time (real and game
// dt, fixed steps, alpha, time since start). Fed seconds from any clock: the Application passes its
// monotonic clock, tests pass exact numbers.
//
//   FrameClock clock(1.0f / 60.0f, 0.25f);
//   clock.Start(now);
//   ...each frame:
//   const FrameClock::Tick tick = clock.Advance(now, time);
//   for (int i = 0; i < tick.steps; ++i) FixedUpdate(time.FixedStep());
class FrameClock {
 public:
  explicit FrameClock(float fixed_step = 1.0f / 60.0f, float max_frame_time = 0.25f);

  struct Tick {
    double seconds = 0.0;  // the frame's measured length, not clamped (stats, quality)
    int steps = 0;         // fixed steps to run this frame
  };

  // The clock's zero: Time::SinceStart counts from here; the first frame is measured from here too.
  void Start(double now);
  // Back from the background: the time away isn't one huge frame, and leftover fixed time is dropped.
  void Restart(double now);
  // One frame: measures it, clamps it to max_frame_time (a breakpoint doesn't hand OnUpdate a
  // 30-second dt), applies the time scale once and fills `time` (its fixed step included).
  Tick Advance(double now, Time& time);
  // A paused frame (Application::Internal().SetPaused): measured like any other (RealDelta, stats), but game
  // time stands still: Delta 0, no steps; the fixed time left over and Alpha are kept for when it goes on.
  Tick Hold(double now, Time& time);
  // A paused frame that moves the game exactly one fixed step (Application::Internal().Step): Delta = the
  // fixed step, one step, whatever the time scale; the fixed time left over and Alpha are kept.
  Tick Step(double now, Time& time);

 private:
  Tick Measure(double now, Time& time);  // the real part of a frame: length, RealDelta, SinceStart

  core::FixedTimestep timestep_;
  float max_frame_time_;
  double start_ = 0.0;
  double previous_ = 0.0;
};

}  // namespace wlw::app
