#pragma once

#include <algorithm>

#include "wlw/anim/ease.h"

namespace wlw::anim {

using EaseFunction = float (*)(float);

// Linear interpolation for anything with + - and * float (float, glm::vec3, ...).
template <typename T>
T Lerp(const T& from, const T& to, float t) {
  return from + (to - from) * t;
}

// A value animated from `from` to `to` over `duration` seconds, after an optional `delay`, shaped
// by an easing function. It's a plain value: the owner calls Update(dt) each frame and reads
// Value(). Nothing points at scene objects, so there's no "tween outlived its target" problem;
// to animate a node, write Value() into it yourself.
//
//   Tween<float> scale(0.5f, 1.0f, 0.15f, ease::OutBack);
//   scale.Update(dt);  node->SetScale(scale.Value());
//
// A default-constructed tween is already finished (Value() = T{}).
template <typename T>
class Tween {
 public:
  Tween() = default;
  Tween(T from, T to, float duration, EaseFunction ease = ease::OutCubic, float delay = 0.0f)
      : from_(from), to_(to), duration_(duration), delay_(delay), ease_(ease) {}

  void Update(float dt) { elapsed_ += dt; }

  // Linear progress in [0, 1]; 0 during the delay.
  float Progress() const {
    if (duration_ <= 0.0f) return elapsed_ >= delay_ ? 1.0f : 0.0f;
    return std::clamp((elapsed_ - delay_) / duration_, 0.0f, 1.0f);
  }

  // The eased value. Back curves can go slightly past `to` (or before `from`) on purpose.
  T Value() const { return Lerp(from_, to_, ease_(Progress())); }

  bool Started() const { return elapsed_ >= delay_; }
  bool Done() const { return elapsed_ >= delay_ + duration_; }

  const T& From() const { return from_; }
  const T& To() const { return to_; }

 private:
  T from_{};
  T to_{};
  float duration_ = 0.0f;
  float delay_ = 0.0f;
  float elapsed_ = 0.0f;
  EaseFunction ease_ = ease::Linear;
};

}  // namespace wlw::anim
