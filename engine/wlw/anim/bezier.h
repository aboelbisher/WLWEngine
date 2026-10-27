#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

#include <glm/glm.hpp>

namespace wlw::anim {

// A cubic Bezier curve: a path from p0 to p3 that leaves p0 heading towards p1 and arrives at p3 coming from p2. The
// path of something flying somewhere (a coin to the counter, a homing spark, a card to a hand): Unity's
// BezierUtility / Splines, Godot's Curve2D / Curve3D segments (their in / out handles are p1 - p0 and p2 - p3),
// Unreal's spline components.
//
//   P(t) = (1-t)^3 p0 + 3 (1-t)^2 t p1 + 3 (1-t) t^2 p2 + t^3 p3,   t in [0, 1]
//
// t is not distance: the point moves faster where the control points are far apart and slower where they bunch up.
// For an even pace (or an easing of the distance travelled, not of t) map through an ArcLength table:
//
//   CubicBezier<glm::vec3> path = CubicBezier<glm::vec3>::Through(from, to, side * 0.4f * distance);
//   ArcLength<glm::vec3> table(path);
//   glm::vec3 p = path.Point(table.TimeAt(ease::InQuad(progress)));  // eased along the path's length
//
// T: float, glm::vec2, glm::vec3 (anything with + - and * float; ArcLength also needs glm::length / std::abs).
// A value type with no state but its four points, so a moving target is a new curve each frame (cheap).
template <typename T>
struct CubicBezier {
  T p0{}, p1{}, p2{}, p3{};

  // The point at `t` (clamped to [0, 1]).
  T Point(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
  }
  // dP/dt at `t`: the direction of travel, as long as the speed per unit of t. Zero only where control points
  // coincide with an end (a straight curve made with Through(from, to, {}) still has a tangent everywhere).
  T Tangent(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return (p1 - p0) * (3.0f * u * u) + (p2 - p1) * (6.0f * u * t) + (p3 - p2) * (3.0f * t * t);
  }

  // A curve from `from` to `to` that bows out by `bend` (an offset, usually across the line between them): its
  // middle passes `bend` x 3/4 away from the straight line. The control points sit at the thirds of that line,
  // pushed by `bend`, so with no bend the curve IS the line, travelled at an even pace.
  static CubicBezier Through(const T& from, const T& to, const T& bend) {
    return {from, from + (to - from) * (1.0f / 3.0f) + bend, from + (to - from) * (2.0f / 3.0f) + bend, to};
  }
};

// A table from distance along a curve to its parameter t, for travelling it at a pace you choose: N + 1 points are
// sampled at even t, their chord lengths summed, and TimeAt(fraction) finds the t at that fraction of the total
// length (a binary search, then linear between two samples). The usual "arc-length reparameterisation" (Unity's
// Splines and Godot's baked curves keep the same kind of table). Accurate to well under 1% of the length for smooth
// curves at the default 16 segments; rebuild it when the curve changes (16 Point() calls).
template <typename T, size_t N = 16>
class ArcLength {
  static_assert(N >= 1, "at least one segment");

 public:
  ArcLength() = default;
  explicit ArcLength(const CubicBezier<T>& curve) {
    T previous = curve.Point(0.0f);
    lengths_[0] = 0.0f;
    for (size_t i = 1; i <= N; ++i) {
      const T point = curve.Point(static_cast<float>(i) / static_cast<float>(N));
      lengths_[i] = lengths_[i - 1] + Distance(previous, point);
      previous = point;
    }
  }

  float Length() const { return lengths_[N]; }

  // The t at `fraction` (clamped to [0, 1]) of the curve's length from its start. A curve of no length: t = fraction.
  float TimeAt(float fraction) const {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    const float total = Length();
    if (total <= 0.0f) return fraction;
    const float target = fraction * total;
    // The first sample at or past the target (exists: the last one is the total).
    const auto it = std::lower_bound(lengths_.begin(), lengths_.end(), target);
    const size_t i = static_cast<size_t>(std::max<std::ptrdiff_t>(it - lengths_.begin(), 1));
    const float span = lengths_[i] - lengths_[i - 1];
    const float within = span > 0.0f ? (target - lengths_[i - 1]) / span : 0.0f;
    return (static_cast<float>(i - 1) + within) / static_cast<float>(N);
  }

 private:
  static float Distance(const T& a, const T& b) {
    if constexpr (std::is_arithmetic_v<T>) {
      return std::abs(b - a);
    } else {
      return glm::length(b - a);
    }
  }

  std::array<float, N + 1> lengths_{};
};

}  // namespace wlw::anim
