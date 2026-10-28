#include "wlw/app/frame_clock.h"

#include <algorithm>

namespace wlw::app {

FrameClock::FrameClock(float fixed_step, float max_frame_time)
    : timestep_(fixed_step, max_frame_time), max_frame_time_(max_frame_time) {}

void FrameClock::Start(double now) {
  start_ = now;
  previous_ = now;
  timestep_.Reset();
}

void FrameClock::Restart(double now) {
  previous_ = now;
  timestep_.Reset();
}

FrameClock::Tick FrameClock::Measure(double now, Time& time) {
  Tick tick;
  tick.seconds = now - previous_;
  previous_ = now;
  time.since_start_ = now - start_;
  time.fixed_step_ = timestep_.Step();
  time.real_delta_ = static_cast<float>(std::clamp(tick.seconds, 0.0, static_cast<double>(max_frame_time_)));
  return tick;
}

FrameClock::Tick FrameClock::Hold(double now, Time& time) {
  const Tick tick = Measure(now, time);
  time.delta_ = 0.0f;
  return tick;
}

FrameClock::Tick FrameClock::Step(double now, Time& time) {
  Tick tick = Measure(now, time);
  time.delta_ = time.fixed_step_;
  tick.steps = 1;
  return tick;
}

FrameClock::Tick FrameClock::Advance(double now, Time& time) {
  Tick tick = Measure(now, time);
  // Game time: real time through the time scale (hit-stop, slow motion). Once per frame.
  time.delta_ = time.scale_.Apply(time.real_delta_);
  tick.steps = timestep_.Advance(time.delta_);
  time.alpha_ = timestep_.Alpha();
  return tick;
}

}  // namespace wlw::app
