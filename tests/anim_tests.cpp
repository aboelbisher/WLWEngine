// Tests for wlw/anim: easing curves and Tween.
#include <algorithm>

#include <glm/glm.hpp>

#include "check.h"
#include "wlw/anim/shake.h"
#include "wlw/anim/tween.h"

using namespace wlw::anim;

static void TestEaseEndpoints() {
  for (EaseFunction f : {ease::Linear, ease::InQuad, ease::OutQuad, ease::InOutQuad, ease::InCubic, ease::OutCubic,
                         ease::InOutCubic, ease::InBack, ease::OutBack}) {
    CHECK(Near(f(0.0f), 0.0f));
    CHECK(Near(f(1.0f), 1.0f));
  }
}

static void TestEaseShapes() {
  // In = slow start (below the straight line), Out = fast start (above it).
  CHECK(ease::InQuad(0.25f) < 0.25f && ease::OutQuad(0.25f) > 0.25f);
  CHECK(ease::InCubic(0.5f) < ease::InQuad(0.5f));  // cubic is "stronger" than quad
  CHECK(Near(ease::InOutQuad(0.5f), 0.5f) && Near(ease::InOutCubic(0.5f), 0.5f));  // symmetric

  // Non-Back curves never leave [0, 1] and never go backwards.
  for (EaseFunction f : {ease::Linear, ease::InQuad, ease::OutQuad, ease::InOutQuad, ease::InCubic, ease::OutCubic,
                         ease::InOutCubic}) {
    float previous = 0.0f;
    for (int i = 1; i <= 100; ++i) {
      const float v = f(i / 100.0f);
      CHECK(v >= previous - 1e-6f && v <= 1.0f + 1e-6f);
      previous = v;
    }
  }

  // Back curves overshoot on purpose: OutBack goes past 1, InBack dips below 0 (~10%).
  float max_out = 0.0f, min_in = 0.0f;
  for (int i = 0; i <= 100; ++i) {
    max_out = std::max(max_out, ease::OutBack(i / 100.0f));
    min_in = std::min(min_in, ease::InBack(i / 100.0f));
  }
  CHECK(max_out > 1.05f && max_out < 1.15f);
  CHECK(min_in < -0.05f && min_in > -0.15f);
}

static void TestTweenTiming() {
  Tween<float> t(10.0f, 20.0f, 1.0f, ease::Linear, 0.5f);  // 0.5 s delay, then 1 s
  CHECK(!t.Started() && !t.Done());
  CHECK(Near(t.Value(), 10.0f));
  t.Update(0.25f);
  CHECK(!t.Started() && Near(t.Value(), 10.0f));  // still waiting
  t.Update(0.75f);                                 // 0.5 s into the motion
  CHECK(t.Started() && Near(t.Progress(), 0.5f) && Near(t.Value(), 15.0f));
  t.Update(10.0f);  // way past the end: clamped, not extrapolated
  CHECK(t.Done() && Near(t.Value(), 20.0f));
}

static void TestTweenEasedAndOtherTypes() {
  Tween<float> eased(0.0f, 1.0f, 1.0f, ease::OutCubic);
  eased.Update(0.5f);
  CHECK(Near(eased.Value(), ease::OutCubic(0.5f)));

  Tween<glm::vec3> move(glm::vec3(0.0f), glm::vec3(2.0f, 4.0f, -6.0f), 2.0f, ease::Linear);
  move.Update(1.0f);
  const glm::vec3 v = move.Value();
  CHECK(Near(v.x, 1.0f) && Near(v.y, 2.0f) && Near(v.z, -3.0f));
}

static void TestTweenEdgeCases() {
  Tween<float> none;  // default: finished, at T{}
  CHECK(none.Done() && Near(none.Value(), 0.0f));

  Tween<float> instant(1.0f, 5.0f, 0.0f);  // zero duration jumps to the end
  CHECK(instant.Done() && Near(instant.Value(), 5.0f));
}

static void TestShake() {
  Shake shake(7);
  CHECK(!shake.Active() && shake.Offset() == glm::vec2(0.0f));  // no trauma, no movement
  // Trauma adds up and is capped at 1; the movement scales with trauma squared.
  shake.AddTrauma(0.3f);
  shake.AddTrauma(0.3f);
  CHECK(Near(shake.Trauma(), 0.6f) && Near(shake.Amount(), 0.36f));
  shake.AddTrauma(5.0f);
  CHECK(Near(shake.Trauma(), 1.0f));
  // Offsets stay within max_offset, and change smoothly between frames (no jitter).
  bool bounded = true, smooth = true, moved = false;
  glm::vec2 previous = shake.Offset();
  for (int i = 0; i < 60; ++i) {
    shake.Update(1.0f / 240.0f);
    const glm::vec2 offset = shake.Offset();
    bounded = bounded && std::abs(offset.x) <= shake.max_offset && std::abs(offset.y) <= shake.max_offset;
    smooth = smooth && glm::length(offset - previous) < shake.max_offset * 0.4f;
    moved = moved || glm::length(offset - previous) > 0.0f;
    previous = offset;
  }
  CHECK(bounded && smooth && moved);
  // It decays at `decay` per second, down to nothing.
  const float before = shake.Trauma();
  shake.Update(0.1f);
  CHECK(Near(shake.Trauma(), before - 0.1f * shake.decay));
  shake.Update(10.0f);
  CHECK(!shake.Active() && shake.Offset() == glm::vec2(0.0f));
  // The same seed and the same updates give the same shake.
  Shake a(3), b(3);
  a.AddTrauma(0.8f);
  b.AddTrauma(0.8f);
  a.Update(0.05f);
  b.Update(0.05f);
  CHECK(a.Offset() == b.Offset());
}

int main() {
  TestEaseEndpoints();
  TestEaseShapes();
  TestTweenTiming();
  TestTweenEasedAndOtherTypes();
  TestTweenEdgeCases();
  TestShake();
  return TestResult("anim");
}
