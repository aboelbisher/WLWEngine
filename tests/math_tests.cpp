// Tests for wlw/core/math.h (Roadmap 2.1): the glm configuration and aliases, constants, scalar helpers (lerps,
// smoothstep, move towards, frame-rate independent decay, angles), vector helpers (safe normalize, 2D cross and
// perpendicular, rotation, segment distance against brute force).
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

#include <glm/glm.hpp>

#include "test_helpers.h"
#include "wlw/core/math.h"
#include "wlw/core/random.h"

TEST_SUITE_BEGIN("math");

using namespace wlw;

TEST_CASE("aliases are glm's types") {
  static_assert(std::is_same_v<Vec3, glm::vec3> && std::is_same_v<Mat4, glm::mat4> && std::is_same_v<Quat, glm::quat>);
  static_assert(sizeof(Vec2) == 8 && sizeof(Vec3) == 12 && sizeof(Vec4) == 16 && sizeof(IVec2) == 8);
  static_assert(sizeof(Quat) == 16 && sizeof(Mat3) == 36 && sizeof(Mat4) == 64);
}

TEST_CASE("constants") {
  CHECK(kPi == doctest::Approx(3.14159265358979));
  CHECK(kTau == 2.0f * kPi);
  CHECK(kHalfPi == 0.5f * kPi);
  CHECK(180.0f * kDegToRad == doctest::Approx(kPi));
  CHECK(kPi * kRadToDeg == doctest::Approx(180.0f));
}

TEST_CASE("lerp, inverse lerp, remap, saturate") {
  CHECK(Lerp(2.0f, 6.0f, 0.25f) == 3.0f);
  CHECK(Lerp(2.0f, 6.0f, 1.5f) == 8.0f);  // not clamped
  CHECK(Lerp(Vec2(0, 0), Vec2(4, 8), 0.5f) == Vec2(2, 4));
  CHECK(InverseLerp(2.0f, 6.0f, 3.0f) == 0.25f);
  CHECK(InverseLerp(5.0f, 5.0f, 9.0f) == 0.0f);  // empty range
  CHECK(Remap(5.0f, 0.0f, 10.0f, 100.0f, 200.0f) == 150.0f);
  CHECK(Remap(5.0f, 10.0f, 0.0f, 0.0f, 1.0f) == 0.5f);
  CHECK(Saturate(-1.0f) == 0.0f);
  CHECK(Saturate(0.3f) == 0.3f);
  CHECK(Saturate(4.0f) == 1.0f);
  CHECK(Saturate(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
  static_assert(Lerp(0.0f, 10.0f, 0.5f) == 5.0f && Saturate(2.0f) == 1.0f);  // usable in constants
}

TEST_CASE("smoothstep matches GLSL's") {
  for (float x = -0.5f; x <= 1.5f; x += 0.01f) {
    const float t = std::clamp((x - 0.2f) / (0.8f - 0.2f), 0.0f, 1.0f);
    CHECK(Near(SmoothStep(0.2f, 0.8f, x), t * t * (3.0f - 2.0f * t), 1e-6f));
  }
  CHECK(SmoothStep(0.0f, 1.0f, 0.5f) == 0.5f);
  CHECK(SmoothStep(1.0f, 0.0f, 0.25f) == doctest::Approx(1.0f - SmoothStep(0.0f, 1.0f, 0.25f)));  // reversed edges
  CHECK(SmoothStep(0.5f, 0.5f, 0.4f) == 0.0f);                                                    // zero width: a step
  CHECK(SmoothStep(0.5f, 0.5f, 0.5f) == 1.0f);
}

TEST_CASE("move towards never overshoots") {
  CHECK(MoveTowards(0.0f, 1.0f, 0.3f) == 0.3f);
  CHECK(MoveTowards(0.9f, 1.0f, 0.3f) == 1.0f);
  CHECK(MoveTowards(1.0f, -1.0f, 0.5f) == 0.5f);
  CHECK(MoveTowards(Vec2(0, 0), Vec2(3, 4), 1.0f) == Vec2(0.6f, 0.8f));
  CHECK(MoveTowards(Vec3(1, 1, 1), Vec3(1, 1, 2), 5.0f) == Vec3(1, 1, 2));
  CHECK(MoveTowards(Vec3(1, 1, 1), Vec3(1, 1, 1), 0.0f) == Vec3(1, 1, 1));  // no 0 / 0
}

TEST_CASE("exp decay is frame-rate independent") {
  auto run = [](int steps) {
    float value = 0.0f;
    for (int i = 0; i < steps; ++i) value = ExpDecay(value, 10.0f, 4.0f, 1.0f / static_cast<float>(steps));
    return value;
  };
  const float exact = 10.0f * (1.0f - std::exp(-4.0f));  // one second
  CHECK(Near(run(30), exact, 1e-3f));
  CHECK(Near(run(144), exact, 1e-3f));
  CHECK(Near(run(1), exact, 1e-4f));
  CHECK(ExpDecay(Vec2(0), Vec2(2, 2), 3.0f, 0.0f) == Vec2(0));  // no time, no change
}

TEST_CASE("angles wrap the short way round") {
  CHECK(Near(WrapAngle(kTau + 0.5f), 0.5f, 1e-6f));
  CHECK(Near(WrapAngle(-kTau - 0.5f), -0.5f, 1e-6f));
  CHECK(Near(WrapAngle(3.0f * kPi + 0.25f), -kPi + 0.25f, 1e-5f));
  CHECK(Near(std::abs(WrapAngle(kPi)), kPi, 1e-6f));
  core::Random rng(5);
  for (int i = 0; i < 200; ++i) {
    const float a = rng.Range(-100.0f, 100.0f);
    const float w = WrapAngle(a);
    CHECK((w >= -kPi - 1e-6f && w <= kPi + 1e-6f));
    CHECK(Near(std::sin(w), std::sin(a), 1e-4f));
    CHECK(Near(std::cos(w), std::cos(a), 1e-4f));
  }
  // 350 degrees to 10 degrees: +20, not -340.
  CHECK(Near(AngleDifference(350.0f * kDegToRad, 10.0f * kDegToRad), 20.0f * kDegToRad, 1e-5f));
  CHECK(Near(AngleDifference(10.0f * kDegToRad, 350.0f * kDegToRad), -20.0f * kDegToRad, 1e-5f));
  CHECK(Near(WrapAngle(LerpAngle(350.0f * kDegToRad, 10.0f * kDegToRad, 0.5f)), 0.0f, 1e-5f));
}

TEST_CASE("nearly equal") {
  CHECK(IsNearlyEqual(1.0f, 1.0f + 1e-7f));
  CHECK_FALSE(IsNearlyEqual(1.0f, 1.001f));
  CHECK(IsNearlyEqual(1.0f, 1.001f, 0.01f));
  CHECK(IsNearlyZero(-1e-7f));
  CHECK_FALSE(IsNearlyZero(0.1f));
}

TEST_CASE("safe normalize, perpendicular, 2D cross, rotate") {
  CHECK(SafeNormalize(Vec2(3, 4), Vec2(0, 1)) == Vec2(0.6f, 0.8f));
  CHECK(SafeNormalize(Vec2(0, 0), Vec2(0, 1)) == Vec2(0, 1));
  CHECK(SafeNormalize(Vec3(0, 0, 1e-9f), Vec3(1, 0, 0)) == Vec3(1, 0, 0));
  CHECK(Near(glm::length(SafeNormalize(Vec3(1, 2, 3), Vec3(0))), 1.0f, 1e-6f));

  CHECK(Perp(Vec2(1, 0)) == Vec2(0, 1));  // counter-clockwise, Y up
  CHECK(glm::dot(Perp(Vec2(2, 7)), Vec2(2, 7)) == 0.0f);
  CHECK(Cross(Vec2(1, 0), Vec2(0, 1)) == 1.0f);
  CHECK(Cross(Vec2(0, 1), Vec2(1, 0)) == -1.0f);
  CHECK(Cross(Vec2(2, 4), Vec2(1, 2)) == 0.0f);  // parallel
  CHECK(Cross(Vec2(3, 0), Vec2(1, 2)) == 6.0f);  // parallelogram area
  CHECK(Cross(Vec2(3, 1), Vec2(-2, 5)) == glm::cross(Vec3(3, 1, 0), Vec3(-2, 5, 0)).z);

  const Vec2 turned = Rotate(Vec2(1, 0), kHalfPi);
  CHECK((Near(turned.x, 0.0f, 1e-6f) && Near(turned.y, 1.0f, 1e-6f)));
  const Vec2 back = Rotate(Rotate(Vec2(2, -3), 0.7f), -0.7f);
  CHECK((Near(back.x, 2.0f, 1e-5f) && Near(back.y, -3.0f, 1e-5f)));
}

TEST_CASE("segment distance matches brute force") {
  core::Random rng(9);
  auto random3 = [&] { return Vec3(rng.Range(-5.0f, 5.0f), rng.Range(-5.0f, 5.0f), rng.Range(-5.0f, 5.0f)); };
  for (int i = 0; i < 100; ++i) {
    const Vec3 a = random3(), b = random3(), p = random3();
    float best = std::numeric_limits<float>::max();
    for (int s = 0; s <= 2000; ++s)
      best = std::min(best, glm::distance(p, Lerp(a, b, static_cast<float>(s) / 2000.0f)));
    CHECK(DistanceToSegment(p, a, b) <= best + 1e-5f);
    CHECK(DistanceToSegment(p, a, b) >= best - 0.01f);  // the sampling's resolution
    const Vec2 a2(a), b2(b), p2(p);
    CHECK(Near(DistanceToSegment(p2, a2, b2), DistanceToSegment(Vec3(p2, 0), Vec3(a2, 0), Vec3(b2, 0)), 1e-5f));
  }
  CHECK(ClosestPointOnSegment(Vec2(5, 5), Vec2(0, 0), Vec2(2, 0)) == Vec2(2, 0));   // past the end
  CHECK(ClosestPointOnSegment(Vec2(-5, 5), Vec2(0, 0), Vec2(2, 0)) == Vec2(0, 0));  // before the start
  CHECK(ClosestPointOnSegment(Vec2(1, 5), Vec2(0, 0), Vec2(2, 0)) == Vec2(1, 0));
  CHECK(DistanceToSegment(Vec2(3, 4), Vec2(0, 0), Vec2(0, 0)) == 5.0f);  // a point segment
}

TEST_SUITE_END();
