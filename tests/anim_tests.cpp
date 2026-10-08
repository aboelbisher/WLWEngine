// Tests for wlw/anim: easing curves and Tween.
#include <algorithm>

#include <glm/glm.hpp>

#include "check.h"
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

int main() {
  TestEaseEndpoints();
  TestEaseShapes();
  TestTweenTiming();
  TestTweenEasedAndOtherTypes();
  TestTweenEdgeCases();
  return TestResult("anim");
}
