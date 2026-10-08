#pragma once

// Easing functions: map animation progress t in [0, 1] to eased progress (0 -> 0, 1 -> 1).
// The shape of the curve is what makes motion feel physical instead of robotic:
//   Out*   fast start, soft landing   - things arriving (snap into place, fly back)
//   In*    slow start, fast finish    - things leaving (shrink away)
//   InOut* soft at both ends          - things moving from one rest to another
//   *Back  overshoots past the end (Out) or pulls back before starting (In) - "snappy", alive
// Formulas follow Robert Penner's easing equations, the de-facto standard (CSS, DOTween, Godot).
namespace wlw::anim::ease {

inline float Linear(float t) { return t; }

inline float InQuad(float t) { return t * t; }
inline float OutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
inline float InOutQuad(float t) {
  if (t < 0.5f) return 2.0f * t * t;
  const float u = -2.0f * t + 2.0f;
  return 1.0f - u * u / 2.0f;
}

inline float InCubic(float t) { return t * t * t; }
inline float OutCubic(float t) {
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}
inline float InOutCubic(float t) {
  if (t < 0.5f) return 4.0f * t * t * t;
  const float u = -2.0f * t + 2.0f;
  return 1.0f - u * u * u / 2.0f;
}

// How far the Back curves overshoot: 1.70158 gives the classic ~10% overshoot.
constexpr float kBackOvershoot = 1.70158f;

inline float InBack(float t) {
  constexpr float c3 = kBackOvershoot + 1.0f;
  return c3 * t * t * t - kBackOvershoot * t * t;
}
inline float OutBack(float t) {
  constexpr float c3 = kBackOvershoot + 1.0f;
  const float u = t - 1.0f;
  return 1.0f + c3 * u * u * u + kBackOvershoot * u * u;
}

}  // namespace wlw::anim::ease
