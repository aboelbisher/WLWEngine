#pragma once

#include <algorithm>

namespace wlw::core {

// Turns variable frame times into a whole number of fixed-size simulation steps.
//
// Rendering runs as fast as the screen allows (frame times vary: 6.9 ms, 16.7 ms, a 200 ms
// hitch...), but game logic is far easier to reason about - and deterministic - when it always
// advances by the same dt. Each frame we add the real elapsed time to an accumulator and run as
// many fixed steps as fit; the leftover carries over to the next frame.
//
//   FixedTimestep stepper(1.0f / 60.0f);
//   int steps = stepper.Advance(frame_seconds);
//   for (int i = 0; i < steps; ++i) Simulate(stepper.Step());
//   Render(stepper.Alpha());   // 0..1: how far we are between the last step and the next
//
// See docs/concepts/01-game-loop.md.
class FixedTimestep {
 public:
  explicit FixedTimestep(float step_seconds = 1.0f / 60.0f, float max_frame_seconds = 0.25f)
      : step_(step_seconds), max_frame_(max_frame_seconds) {}

  // Adds one frame's elapsed time and returns how many fixed steps to simulate now.
  int Advance(double frame_seconds) {
    // Clamp huge frames (debugger breakpoint, window drag, slow load). Otherwise we'd try to
    // catch up with hundreds of steps, which makes the next frame slow too: the "spiral of death".
    frame_seconds = std::clamp(frame_seconds, 0.0, static_cast<double>(max_frame_));
    accumulator_ += frame_seconds;

    int steps = 0;
    while (accumulator_ >= step_) {
      accumulator_ -= step_;
      ++steps;
    }
    return steps;
  }

  float Step() const { return step_; }

  // Fraction of a step left in the accumulator, in [0, 1). Use it to interpolate rendering
  // between the previous and the current simulation state so motion looks smooth.
  float Alpha() const { return static_cast<float>(accumulator_ / step_); }

  void Reset() { accumulator_ = 0.0; }

 private:
  float step_;
  float max_frame_;
  double accumulator_ = 0.0;  // double: avoids precision drift over long sessions
};

}  // namespace wlw::core
