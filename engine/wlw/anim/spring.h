#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "wlw/core/math.h"

namespace wlw::anim {

// A damped spring (a damped harmonic oscillator) pulling a value towards `target`.
//
// A Tween is a fixed curve: it knows its whole path in advance. A spring only knows where it is,
// how fast it moves and where it is pulled to, so it can be re-aimed or kicked again at any moment
// and always continues smoothly from its current motion. That makes it the tool for reactions:
// a block knocked by a nearby clear wobbles and settles, and a second knock adds to the first.
//
// Set by two numbers instead of stiffness and mass:
//   frequency (Hz): how fast it swings (the undamped oscillations per second),
//   damping (zeta): < 1 bouncy (overshoots, the lower the more), 1 critical (the fastest arrival
//                   without overshoot), > 1 sluggish.
//
// Each Update(dt) steps the EXACT solution of x'' = -w^2 (x - target) - 2 zeta w x' (w = 2 pi
// frequency), so it is stable at any dt and the same motion at 30 or 60 Hz; the usual
// "velocity += force * dt" (semi-implicit Euler) gains energy or explodes with a stiff spring
// and a long frame.
//
// Like a Tween, the game owns it and samples it. T: float, glm::vec2, glm::vec3 (anything with
// + - and * float, and a length below).
//
//   Spring<glm::vec3> offset(5.0f, 0.35f);  // 5 Hz, bouncy
//   offset.Kick({0.0f, 0.0f, -2.0f});       // knocked down
//   offset.Update(dt);  node->SetPosition(rest + offset.Value());
template <typename T>
class Spring {
 public:
  Spring() = default;
  explicit Spring(float frequency_hz, float damping_ratio = 1.0f, T start = T{})
      : frequency(frequency_hz), damping(damping_ratio), target(start), value_(start) {}

  float frequency = 4.0f;  // Hz
  float damping = 1.0f;    // zeta
  T target{};

  // Adds velocity (units per second): an impulse.
  void Kick(const T& velocity) { velocity_ = velocity_ + velocity; }
  // Jumps to `value` and stops there (velocity 0); from there it springs towards the target.
  void SnapTo(const T& value) {
    value_ = value;
    velocity_ = T{};
  }

  void Update(float dt) {
    if (dt <= 0.0f) return;
    const float w = kTau * std::max(frequency, 0.0f);
    if (w <= 0.0f) {  // no spring: it drifts at its velocity
      value_ = value_ + velocity_ * dt;
      return;
    }
    // The motion is linear in the start offset y0 and velocity v0: y = a y0 + b v0, v = c y0 + d v0.
    const float zeta = std::max(damping, 0.0f);
    float a, b, c, d;
    if (zeta < 0.9999f) {  // underdamped: a decaying wave
      const float wd = w * std::sqrt(1.0f - zeta * zeta);
      const float decay = std::exp(-zeta * w * dt);
      const float cw = std::cos(wd * dt), sw = std::sin(wd * dt);
      a = decay * (cw + zeta * w / wd * sw);
      b = decay * sw / wd;
      c = -decay * w * w / wd * sw;
      d = decay * (cw - zeta * w / wd * sw);
    } else if (zeta <= 1.0001f) {  // critical
      const float decay = std::exp(-w * dt);
      a = decay * (1.0f + w * dt);
      b = decay * dt;
      c = -decay * w * w * dt;
      d = decay * (1.0f - w * dt);
    } else {  // overdamped: two decaying exponentials, y = c1 e^(r1 t) + c2 e^(r2 t)
      const float root = std::sqrt(zeta * zeta - 1.0f);
      const float r1 = -w * (zeta - root), r2 = -w * (zeta + root);
      const float e1 = std::exp(r1 * dt), e2 = std::exp(r2 * dt);
      const float k = 1.0f / (r2 - r1);
      // c2 = (v0 - r1 y0) / (r2 - r1), c1 = y0 - c2.
      a = e1 + (e1 - e2) * r1 * k;
      b = (e2 - e1) * k;
      c = r1 * e1 + (r1 * e1 - r2 * e2) * r1 * k;
      d = (r2 * e2 - r1 * e1) * k;
    }
    const T y0 = value_ - target;
    const T v0 = velocity_;
    value_ = target + y0 * a + v0 * b;
    velocity_ = y0 * c + v0 * d;
  }

  const T& Value() const { return value_; }
  const T& Velocity() const { return velocity_; }
  // Near enough the target and slow enough to call it settled.
  bool AtRest(float epsilon = 1e-3f) const {
    return Length(value_ - target) <= epsilon && Length(velocity_) <= epsilon;
  }

 private:
  static float Length(float v) { return std::abs(v); }
  template <typename V>
  static float Length(const V& v) {
    return glm::length(v);
  }

  T value_{};
  T velocity_{};
};

}  // namespace wlw::anim
