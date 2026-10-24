#pragma once

// The engine's maths: glm behind short wlw names (Roadmap D1), constants, and the small helpers every game
// otherwise writes again (lerp, smoothstep, angle wrapping, safe normalize, segment distance).
//
// Conventions (D1, Roadmap 2.4), the same in every engine system and expected from every game:
//   Z up, +Y forward, +X right, right-handed (Right x Forward = Up), like Blender, O3DE and CryEngine.
//   Meters, radians, seconds. A positive turn about an axis is counter-clockwise looking down that axis at the
//   origin (a positive yaw about +Z turns +Y towards -X). Front faces wind counter-clockwise (seen from outside).
//   A model faces -Y (kModelFront: towards a camera looking along +Y), as glTF's +Z front becomes on import.
//   Cameras look along their +Y with +Z up on screen; only the GPU's view space is GL's (looking down -Z, +Y up:
//   Camera3D's view matrix turns one into the other). Cube maps keep the usual Y-up layout of skybox images.
//   2D (ui::Canvas, screen points, Input): x right, y DOWN, origin top-left; texture uv (0, 0) = the first row
//   uploaded (an image's top row). Colours are sRGB-encoded as authored (core/color.h).
// Clip space is GL's (z in -1..1). glm is configured once, in CMake (wlw_glm's INTERFACE definitions), so the
// engine and every game see the same types:
//   GLM_ENABLE_EXPERIMENTAL the gtx headers (matrix_decompose, ...)
// GLM_FORCE_CTOR_INIT (vectors starting at 0, matrices at identity) stays off: glm's operators then build every
// result twice, and nested transforms ran 21-30% slower (2.1, MSVC). A default-constructed Vec3 / Mat4 holds
// garbage, so initialise them (Vec3 v{0.0f}, Mat4 m{1.0f}); engine structs give their members defaults.
#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wlw {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;
using Quat = glm::quat;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kTau = 2.0f * kPi;  // a full turn
inline constexpr float kHalfPi = 0.5f * kPi;
inline constexpr float kDegToRad = kPi / 180.0f;  // degrees * kDegToRad = radians
inline constexpr float kRadToDeg = 180.0f / kPi;  // radians * kRadToDeg = degrees

// ---- The world's axes (see Conventions above) ----
inline constexpr Vec3 kRight{1.0f, 0.0f, 0.0f};
inline constexpr Vec3 kLeft{-1.0f, 0.0f, 0.0f};
inline constexpr Vec3 kForward{0.0f, 1.0f, 0.0f};
inline constexpr Vec3 kBack{0.0f, -1.0f, 0.0f};
inline constexpr Vec3 kUp{0.0f, 0.0f, 1.0f};
inline constexpr Vec3 kDown{0.0f, 0.0f, -1.0f};
inline constexpr Vec3 kModelFront{0.0f, -1.0f, 0.0f};  // the way a model faces (glTF's +Z front, imported)

// A point or direction from a Y-up file (glTF, OBJ: +Y up, -Z forward) in the engine's axes: a quarter turn about
// X, so +Y -> +Z and -Z -> +Y. Lengths and handedness stay.
constexpr Vec3 FromYUp(const Vec3& v) { return {v.x, -v.z, v.y}; }
// Back: the engine's axes into a Y-up file's (and GL's view space: x, y up, -z forward).
constexpr Vec3 ToYUp(const Vec3& v) { return {v.x, v.z, -v.y}; }

// ---- Scalars (most work on vectors and colours too: anything with + - and * float) ----

// a at t = 0, b at t = 1; t isn't clamped (t = 2 goes on past b).
template <typename T>
constexpr T Lerp(const T& a, const T& b, float t) {
  return a + (b - a) * t;
}

// Where `value` sits between a and b: 0 at a, 1 at b (not clamped). a == b gives 0.
constexpr float InverseLerp(float a, float b, float value) { return a == b ? 0.0f : (value - a) / (b - a); }

// `value` moved from the range in_a..in_b to out_a..out_b (not clamped).
constexpr float Remap(float value, float in_a, float in_b, float out_a, float out_b) {
  return Lerp(out_a, out_b, InverseLerp(in_a, in_b, value));
}

// Clamped to 0..1 (HLSL's saturate). NaN gives 0.
constexpr float Saturate(float x) { return x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f; }

// 0 below edge0, 1 above edge1, Hermite's smooth curve (3t^2 - 2t^3) between, like GLSL's smoothstep.
// edge0 == edge1 is a step at that edge.
constexpr float SmoothStep(float edge0, float edge1, float x) {
  if (edge0 == edge1) return x < edge0 ? 0.0f : 1.0f;
  const float t = Saturate((x - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

// `current` moved towards `target` by at most `max_delta` (>= 0), never past it (Unity's MoveTowards).
constexpr float MoveTowards(float current, float target, float max_delta) {
  return current < target ? std::min(current + max_delta, target) : std::max(current - max_delta, target);
}
// The same for a point: along the straight line, never past `target`.
template <typename V>
V MoveTowards(const V& current, const V& target, float max_delta) {
  const V to = target - current;
  const float distance = glm::length(to);
  return distance <= max_delta || distance == 0.0f ? target : current + to * (max_delta / distance);
}

// Frame-rate independent smoothing: `current` follows `target`, closing the gap by the same fraction every
// second whatever the frame rate (a + (b - a) * k per frame is not: it moves faster at 120 fps than at 30).
// `decay` ~1 is slow, ~25 is nearly instant; the gap shrinks by e^-decay per second.
template <typename T>
T ExpDecay(const T& current, const T& target, float decay, float dt) {
  return Lerp(target, current, std::exp(-decay * dt));
}

// An angle brought into -pi..pi (radians).
inline float WrapAngle(float radians) { return std::remainder(radians, kTau); }  // exact (IEEE remainder)
// The shortest turn from `from` to `to`, -pi..pi (radians): 350 degrees to 10 degrees is +20, not -340.
inline float AngleDifference(float from, float to) { return WrapAngle(to - from); }
// Lerp between angles the short way round.
inline float LerpAngle(float from, float to, float t) { return from + AngleDifference(from, to) * t; }

constexpr bool IsNearlyEqual(float a, float b, float tolerance = 1e-6f) { return (a > b ? a - b : b - a) <= tolerance; }
constexpr bool IsNearlyZero(float x, float tolerance = 1e-6f) { return IsNearlyEqual(x, 0.0f, tolerance); }

// ---- Vectors ----

// v at length 1, or `fallback` when v is (nearly) zero (glm::normalize would give NaNs).
template <typename V>
V SafeNormalize(const V& v, const V& fallback) {
  const float length_squared = glm::dot(v, v);
  return length_squared > 1e-12f ? v / std::sqrt(length_squared) : fallback;
}

// v turned a quarter turn counter-clockwise in a plane with y up ((1, 0) -> (0, 1); the ground XY seen from
// above). On a y-down screen (Canvas) that is clockwise.
constexpr Vec2 Perp(const Vec2& v) { return {-v.y, v.x}; }

// The 2D cross product: z of the 3D cross product. > 0 when b is counter-clockwise of a (y up), 0 when they're
// parallel; |result| = the area of the parallelogram they span.
constexpr float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

// v turned by `radians`, counter-clockwise in a plane with y up.
inline Vec2 Rotate(const Vec2& v, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// The point of the segment a-b nearest to p (a when a == b).
template <typename V>
V ClosestPointOnSegment(const V& p, const V& a, const V& b) {
  const V ab = b - a;
  const float length_squared = glm::dot(ab, ab);
  if (length_squared == 0.0f) return a;
  return a + ab * Saturate(glm::dot(p - a, ab) / length_squared);
}
template <typename V>
float DistanceToSegment(const V& p, const V& a, const V& b) {
  return glm::distance(p, ClosestPointOnSegment(p, a, b));
}

}  // namespace wlw

// ---- Fast trigonometry (the hot loops) ----

namespace wlw::core {

struct SinCosResult {
  float sin;
  float cos;
};

// sin and cos of one angle (radians) together, faster than std::sin + std::cos: one range
// reduction for both, two short polynomials, no calls (fx/particles instances 5k: see CHANGELOG
// 1.6). Max absolute error 1.2e-7 against double precision over +-60 rad (std::sin's float is
// 6e-8); meant for visuals (particle rotations). Exact reduction up to |radians| ~ 1e5; past
// ~3e9 the quadrant no longer fits an int.
// Like DirectXMath's XMScalarSinCos and Unreal's FMath::SinCos.
//
// How: x = q * pi/2 + r with |r| <= pi/4 (Cody-Waite: pi/2 in 3 parts, the first with few enough
// bits that q * part is exact, so r keeps its precision), then Cephes' minimax polynomials for sin
// and cos on [-pi/4, pi/4], swapped and negated by the quadrant q mod 4.
inline SinCosResult SinCos(float radians) {
  constexpr float kTwoOverPi = 0.636619772367581343f;
  constexpr float kPiOver2A = 1.5703125f;  // 8 significant bits: q * it is exact for |q| < 2^16
  constexpr float kPiOver2B = 4.837512969970703125e-4f;
  constexpr float kPiOver2C = 7.54978995489188216e-8f;

  // The nearest quadrant: floor(x + 0.5). Not a branch on the sign (random angles mispredict it: 40%
  // slower on MSVC), not truncation fixed up below zero (int <-> float round trips: 45% slower), not
  // the (x + 1.5 * 2^23) - 1.5 * 2^23 trick (fast-math folds it away). One instruction on ARM64.
  const float qf = std::floor(radians * kTwoOverPi + 0.5f);
  const auto q = static_cast<int>(qf);
  const float r = ((radians - qf * kPiOver2A) - qf * kPiOver2B) - qf * kPiOver2C;
  const float z = r * r;

  const float s = ((-1.9515295891e-4f * z + 8.3321608736e-3f) * z - 1.6666654611e-1f) * z * r + r;
  const float c =
      ((2.443315711809948e-5f * z - 1.388731625493765e-3f) * z + 4.166664568298827e-2f) * z * z - 0.5f * z + 1.0f;

  // Quadrant q mod 4 (two's complement: right for negative q too): 0 = (s, c), 1 = (c, -s),
  // 2 = (-s, -c), 3 = (-c, s). Arithmetic, not branches or a switch (compilers branch on float
  // selects; random angles mispredict them): the swap is a blend by 0 / 1, exact.
  const auto swap = static_cast<float>(q & 1);
  const float keep = 1.0f - swap;
  const auto sin_sign = static_cast<float>(1 - (q & 2));        // -1 in quadrants 2, 3
  const auto cos_sign = static_cast<float>(1 - ((q + 1) & 2));  // -1 in quadrants 1, 2
  return {(s * keep + c * swap) * sin_sign, (c * keep + s * swap) * cos_sign};
}

}  // namespace wlw::core
