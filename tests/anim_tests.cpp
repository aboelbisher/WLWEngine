// Tests for wlw/anim: easing curves, Tween, Shake, Timeline, Spring, Curve and Gradient.
#include <algorithm>

#include <glm/glm.hpp>

#include "test_helpers.h"
#include "wlw/anim/curve.h"
#include "wlw/anim/gradient.h"
#include "wlw/anim/shake.h"
#include "wlw/anim/spring.h"
#include "wlw/anim/timeline.h"
#include "wlw/anim/tween.h"

TEST_SUITE_BEGIN("anim");

using namespace wlw::anim;

TEST_CASE("ease endpoints") {
  for (EaseFunction f : {ease::Linear, ease::InQuad, ease::OutQuad, ease::InOutQuad, ease::InCubic, ease::OutCubic,
                         ease::InOutCubic, ease::InBack, ease::OutBack}) {
    CHECK(Near(f(0.0f), 0.0f));
    CHECK(Near(f(1.0f), 1.0f));
  }
}

TEST_CASE("ease shapes") {
  // In = slow start (below the straight line), Out = fast start (above it).
  CHECK((ease::InQuad(0.25f) < 0.25f && ease::OutQuad(0.25f) > 0.25f));
  CHECK(ease::InCubic(0.5f) < ease::InQuad(0.5f));                                   // cubic is "stronger" than quad
  CHECK((Near(ease::InOutQuad(0.5f), 0.5f) && Near(ease::InOutCubic(0.5f), 0.5f)));  // symmetric

  // Non-Back curves never leave [0, 1] and never go backwards.
  for (EaseFunction f :
       {ease::Linear, ease::InQuad, ease::OutQuad, ease::InOutQuad, ease::InCubic, ease::OutCubic, ease::InOutCubic}) {
    float previous = 0.0f;
    for (int i = 1; i <= 100; ++i) {
      const float v = f(i / 100.0f);
      CHECK((v >= previous - 1e-6f && v <= 1.0f + 1e-6f));
      previous = v;
    }
  }

  // Back curves overshoot on purpose: OutBack goes past 1, InBack dips below 0 (~10%).
  float max_out = 0.0f, min_in = 0.0f;
  for (int i = 0; i <= 100; ++i) {
    max_out = std::max(max_out, ease::OutBack(i / 100.0f));
    min_in = std::min(min_in, ease::InBack(i / 100.0f));
  }
  CHECK((max_out > 1.05f && max_out < 1.15f));
  CHECK((min_in < -0.05f && min_in > -0.15f));
}

TEST_CASE("tween timing") {
  Tween<float> t(10.0f, 20.0f, 1.0f, ease::Linear, 0.5f);  // 0.5 s delay, then 1 s
  CHECK((!t.Started() && !t.Done()));
  CHECK(Near(t.Value(), 10.0f));
  t.Update(0.25f);
  CHECK((!t.Started() && Near(t.Value(), 10.0f)));  // still waiting
  t.Update(0.75f);                                  // 0.5 s into the motion
  CHECK((t.Started() && Near(t.Progress(), 0.5f) && Near(t.Value(), 15.0f)));
  t.Update(10.0f);  // way past the end: clamped, not extrapolated
  CHECK((t.Done() && Near(t.Value(), 20.0f)));
}

TEST_CASE("tween eased and other types") {
  Tween<float> eased(0.0f, 1.0f, 1.0f, ease::OutCubic);
  eased.Update(0.5f);
  CHECK(Near(eased.Value(), ease::OutCubic(0.5f)));

  Tween<glm::vec3> move(glm::vec3(0.0f), glm::vec3(2.0f, 4.0f, -6.0f), 2.0f, ease::Linear);
  move.Update(1.0f);
  const glm::vec3 v = move.Value();
  CHECK((Near(v.x, 1.0f) && Near(v.y, 2.0f) && Near(v.z, -3.0f)));
}

TEST_CASE("tween edge cases") {
  Tween<float> none;  // default: finished, at T{}
  CHECK((none.Done() && Near(none.Value(), 0.0f)));

  Tween<float> instant(1.0f, 5.0f, 0.0f);  // zero duration jumps to the end
  CHECK((instant.Done() && Near(instant.Value(), 5.0f)));
}

TEST_CASE("shake") {
  Shake shake(7);
  CHECK((!shake.Active() && shake.Offset() == glm::vec2(0.0f)));  // no trauma, no movement
  // Trauma adds up and is capped at 1; the movement scales with trauma squared.
  shake.AddTrauma(0.3f);
  shake.AddTrauma(0.3f);
  CHECK((Near(shake.Trauma(), 0.6f) && Near(shake.Amount(), 0.36f)));
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
  CHECK((bounded && smooth && moved));
  // It decays at `decay` per second, down to nothing.
  const float before = shake.Trauma();
  shake.Update(0.1f);
  CHECK(Near(shake.Trauma(), before - 0.1f * shake.decay));
  shake.Update(10.0f);
  CHECK((!shake.Active() && shake.Offset() == glm::vec2(0.0f)));
  // The same seed and the same updates give the same shake.
  Shake a(3), b(3);
  a.AddTrauma(0.8f);
  b.AddTrauma(0.8f);
  a.Update(0.05f);
  b.Update(0.05f);
  CHECK(a.Offset() == b.Offset());
}

TEST_CASE("timeline placement") {
  Timeline t;
  const auto a = t.Then(0.5f);         // 0.0 .. 0.5
  const auto b = t.Then(1.0f, 0.25f);  // 0.75 .. 1.75
  const auto c = t.With(0.0f);         // a cue at b's start
  const auto d = t.With(0.2f, 0.5f);   // 1.25 .. 1.45 (relative to c, the last added)
  const auto e = t.At(3.0f, 0.5f);     // 3.0 .. 3.5
  const auto f = t.Then(0.0f);         // a cue at e's end
  CHECK((Near(t.Start(a), 0.0f) && Near(t.End(a), 0.5f)));
  CHECK((Near(t.Start(b), 0.75f) && Near(t.End(b), 1.75f)));
  CHECK((Near(t.Start(c), 0.75f) && Near(t.End(c), 0.75f)));
  CHECK((Near(t.Start(d), 1.25f) && Near(t.End(d), 1.45f)));
  CHECK((Near(t.Start(e), 3.0f) && Near(t.Start(f), 3.5f)));
  CHECK(Near(t.Duration(), 3.5f));
  CHECK(Near(Timeline().Duration(), 0.0f));
  const auto g = t.At(-1.0f, -2.0f);                                  // negatives clamp to 0
  CHECK((g == 6 && Near(t.Start(g), 0.0f) && Near(t.End(g), 0.0f)));  // handles are indices, in order
}

TEST_CASE("timeline progress") {
  Timeline t;
  const auto a = t.Then(1.0f, 0.5f);  // 0.5 .. 1.5
  const auto cue = t.Then(0.0f);      // 1.5
  CHECK((!t.Started(a) && Near(t.Progress(a), 0.0f) && Near(t.Elapsed(a), 0.0f)));
  t.Update(1.0f);
  CHECK((t.Started(a) && !t.Done(a) && Near(t.Progress(a), 0.5f) && Near(t.Elapsed(a), 0.5f)));
  CHECK((!t.Started(cue) && Near(t.Progress(cue), 0.0f) && !t.Finished()));
  t.Update(5.0f);  // far past the end: clamped
  CHECK((t.Done(a) && Near(t.Progress(a), 1.0f) && Near(t.Progress(cue), 1.0f) && t.Finished()));
  CHECK(Near(t.Elapsed(a), 5.5f));    // not capped: for effects that loop inside a beat
  CHECK(Near(t.Progress(99), 1.0f));  // an unknown handle reads as a cue at 0, not a crash
}

TEST_CASE("timeline cues are crossed once") {
  Timeline t;
  const auto first = t.At(0.0f, 0.0f);
  const auto one = t.At(1.0f, 0.0f);
  const auto two = t.At(1.2f, 0.0f);
  const auto three = t.At(3.0f, 0.0f);
  CHECK(!t.Crossed(first));  // nothing happened before the first update
  t.Update(0.5f);
  CHECK((t.Crossed(first) && !t.Crossed(one)));  // the first update counts time 0
  t.Update(0.5f);
  CHECK((!t.Crossed(first) && t.Crossed(one) && !t.Crossed(two)));  // landing exactly on a cue counts
  t.Update(1.0f);                                                   // a long frame: 1.0 .. 2.0 jumps over 1.2
  CHECK((!t.Crossed(one) && t.Crossed(two)));
  t.Update(0.0f);
  CHECK(!t.Crossed(two));  // once only
  CHECK(!t.Skipped());
  t.Skip();  // the rest at once
  CHECK((t.Skipped() && t.Finished() && t.Crossed(three) && !t.Crossed(two)));
  t.Skip();  // already at the end: nothing new
  CHECK(!t.Crossed(three));

  t.Restart();
  CHECK((Near(t.Time(), 0.0f) && !t.Skipped() && !t.Started(one)));
  t.Skip();  // a skip before any update crosses everything
  CHECK((t.Crossed(first) && t.Crossed(one) && t.Crossed(three)));
  t.Clear();
  CHECK((Near(t.Duration(), 0.0f) && t.Finished()));
}

// Steps `spring` for `seconds` in steps of `dt`, returning the furthest it went past the target (the
// target minus the value, for a spring that starts below it).
static float RunSpring(Spring<float>& spring, float seconds, float dt) {
  float overshoot = 0.0f;
  for (float t = 0.0f; t < seconds - 1e-6f; t += dt) {
    spring.Update(dt);
    overshoot = std::max(overshoot, spring.Value() - spring.target);
  }
  return overshoot;
}

TEST_CASE("spring settles on its target") {
  for (float zeta : {0.2f, 0.7f, 1.0f, 2.0f}) {
    Spring<float> s(3.0f, zeta);
    s.target = 1.0f;
    RunSpring(s, 5.0f, 1.0f / 60.0f);
    CHECK((Near(s.Value(), 1.0f, 1e-3f) && s.AtRest()));
  }
  Spring<float> idle(3.0f, 0.5f, 2.0f);  // starts on its target: stays there
  idle.Update(1.0f);
  CHECK((Near(idle.Value(), 2.0f) && idle.AtRest()));
}

TEST_CASE("spring damping: bouncy overshoots, critical doesn't") {
  Spring<float> bouncy(3.0f, 0.3f);
  bouncy.target = 1.0f;
  CHECK(RunSpring(bouncy, 2.0f, 1.0f / 60.0f) > 0.3f);  // zeta 0.3: ~37% past the target

  Spring<float> critical(3.0f, 1.0f);
  critical.target = 1.0f;
  CHECK(RunSpring(critical, 2.0f, 1.0f / 60.0f) <= 1e-6f);

  Spring<float> slow(3.0f, 3.0f);
  slow.target = 1.0f;
  CHECK(RunSpring(slow, 2.0f, 1.0f / 60.0f) <= 1e-6f);
  CHECK(slow.Value() < critical.Value());  // overdamped arrives later
}

TEST_CASE("spring is frame-rate independent") {
  // The exact solution: 60 Hz, 30 Hz and one big step land on the same value and velocity.
  for (float zeta : {0.3f, 1.0f, 1.8f}) {
    Spring<float> a(2.5f, zeta), b(2.5f, zeta), c(2.5f, zeta);
    for (Spring<float>* s : {&a, &b, &c}) {
      s->target = 1.0f;
      s->Kick(-3.0f);
    }
    RunSpring(a, 0.5f, 1.0f / 60.0f);
    RunSpring(b, 0.5f, 1.0f / 30.0f);
    c.Update(0.5f);
    CHECK((Near(a.Value(), b.Value(), 1e-4f) && Near(a.Value(), c.Value(), 1e-4f)));
    CHECK((Near(a.Velocity(), b.Velocity(), 1e-3f) && Near(a.Velocity(), c.Velocity(), 1e-3f)));
  }
  Spring<float> stiff(30.0f, 0.1f);  // a stiff spring and a long frame: still bounded
  stiff.target = 1.0f;
  stiff.Update(0.25f);
  CHECK(std::abs(stiff.Value()) < 2.0f);
}

TEST_CASE("spring kicks add velocity") {
  Spring<float> s(4.0f, 0.4f);
  s.Kick(2.0f);
  CHECK(Near(s.Velocity(), 2.0f));
  s.Kick(1.0f);
  CHECK(Near(s.Velocity(), 3.0f));
  s.Update(0.02f);
  CHECK(s.Value() > 0.0f);  // moves the kick's way first...
  RunSpring(s, 4.0f, 1.0f / 60.0f);
  CHECK(s.AtRest());  // ...and settles back on the target (0)

  s.SnapTo(5.0f);  // a jump: at rest there, then pulled back
  CHECK((Near(s.Value(), 5.0f) && Near(s.Velocity(), 0.0f) && !s.AtRest()));
}

TEST_CASE("spring of vectors") {
  Spring<glm::vec3> v(5.0f, 0.35f);
  v.Kick({0.0f, 1.0f, -2.0f});
  v.Update(0.02f);
  CHECK((v.Value().y > 0.0f && v.Value().z < 0.0f && Near(v.Value().x, 0.0f)));
  for (int i = 0; i < 240; ++i) v.Update(1.0f / 60.0f);
  CHECK(v.AtRest());

  // Each component moves like a float spring with the same settings.
  Spring<float> y(5.0f, 0.35f);
  y.Kick(1.0f);
  Spring<glm::vec2> xy(5.0f, 0.35f);
  xy.Kick({0.0f, 1.0f});
  y.Update(0.13f);
  xy.Update(0.13f);
  CHECK(Near(xy.Value().y, y.Value()));
}

// ---- Curve: Hermite keys, baked ----

TEST_CASE("curve linear and constant") {
  const Curve line = Curve::Linear({{0.0f, 1.0f}, {0.5f, 3.0f}, {1.0f, 0.0f}});
  CHECK((Near(line.Evaluate(0.25f), 2.0f) && Near(line.Evaluate(0.5f), 3.0f) && Near(line.Evaluate(0.75f), 1.5f)));
  CHECK((Near(line.Evaluate(-1.0f), 1.0f) && Near(line.Evaluate(2.0f), 0.0f)));  // held past the ends
  CHECK((Near(Curve::Constant(0.4f).Evaluate(-3.0f), 0.4f) && Near(Curve::Constant(0.4f).Sample(7.0f), 0.4f)));
  CHECK((Near(Curve().Evaluate(0.3f), 1.0f) && Near(Curve().Sample(0.3f), 1.0f)));  // default: a neutral multiplier
}

TEST_CASE("curve smooth passes through its keys") {
  const Curve hump = Curve::Smooth({{0.0f, 0.0f}, {0.3f, 1.0f}, {1.0f, 0.0f}});
  CHECK((Near(hump.Evaluate(0.0f), 0.0f) && Near(hump.Evaluate(0.3f), 1.0f) && Near(hump.Evaluate(1.0f), 0.0f)));
  // The inner key is a peak (its tangent is the slope from 0 to 1: flat here), the ends start flat.
  CHECK((hump.Evaluate(0.29f) < 1.0f && hump.Evaluate(0.31f) < 1.0f));
  CHECK(hump.Evaluate(0.02f) < 0.02f);  // flat start: slower than a straight line (0.067)
  // Rising before the peak, falling after it.
  bool shaped = true;
  for (float t = 0.01f; t < 0.3f; t += 0.01f) shaped = shaped && hump.Evaluate(t) >= hump.Evaluate(t - 0.01f);
  for (float t = 0.31f; t <= 1.0f; t += 0.01f) shaped = shaped && hump.Evaluate(t) <= hump.Evaluate(t - 0.01f);
  CHECK(shaped);
}

TEST_CASE("curve tangents, keys in any order, baked table") {
  // Explicit tangents: 0 -> 1 leaving with slope 2 and arriving flat = an ease-out (above the line at the middle).
  const Curve ease_out({Curve::Key{0.0f, 0.0f, 0.0f, 2.0f}, Curve::Key{1.0f, 1.0f, 0.0f, 0.0f}});
  CHECK(Near(ease_out.Evaluate(0.5f), 0.75f));  // h00*0 + h10*2 + h01*1 + h11*0 at s = 0.5: 0.25 + 0.5
  // Keys are sorted, AddKey keeps the order, and the time range may be anything.
  Curve curve = Curve::Linear({{4.0f, 10.0f}, {2.0f, 0.0f}});
  CHECK((curve.Keys().front().time == 2.0f && Near(curve.Evaluate(3.0f), 5.0f) && Near(curve.Sample(3.0f), 5.0f)));
  curve.AddKey({3.0f, 8.0f, 0.0f, 0.0f});
  CHECK((curve.Keys()[1].time == 3.0f && Near(curve.Sample(3.0f), 8.0f, 0.3f)));
  // The table is close to the exact curve everywhere (64 points, linear between).
  const Curve hump = Curve::Smooth({{0.0f, 0.0f}, {0.3f, 1.0f}, {1.0f, 0.0f}});
  float worst = 0.0f;
  for (float t = -0.1f; t <= 1.1f; t += 0.001f) worst = std::max(worst, std::abs(hump.Sample(t) - hump.Evaluate(t)));
  CHECK(worst < 0.005f);  // h² / 8 x the largest bend: 0.0159² / 8 x 67 = 0.0021
}

// ---- Gradient ----

TEST_CASE("gradient linear, constant, ends") {
  const wlw::Color white{1, 1, 1, 1}, red{1, 0, 0, 1}, clear{0, 0, 1, 0};
  const Gradient fade({{0.0f, white}, {0.5f, red}, {1.0f, clear}});
  const wlw::Color quarter = fade.Evaluate(0.25f), three_quarters = fade.Evaluate(0.75f);
  CHECK((Near(quarter.r, 1.0f) && Near(quarter.g, 0.5f) && Near(quarter.a, 1.0f)));
  CHECK((Near(three_quarters.r, 0.5f) && Near(three_quarters.b, 0.5f) && Near(three_quarters.a, 0.5f)));
  CHECK((fade.Evaluate(-1.0f) == white && fade.Evaluate(2.0f) == clear));
  const wlw::Color sampled = fade.Sample(0.25f);
  CHECK((Near(sampled.g, 0.5f, 0.01f) && Near(fade.Sample(1.0f).a, 0.0f)));

  const Gradient steps({{0.0f, white}, {0.5f, red}, {1.0f, clear}}, Gradient::Interpolation::Constant);
  CHECK((steps.Evaluate(0.49f) == white && steps.Evaluate(0.5f) == red && steps.Evaluate(0.99f) == red));
  CHECK((steps.Sample(0.4f) == white && steps.Sample(0.6f) == red && steps.Sample(1.0f) == clear));
  CHECK((Gradient().Sample(0.5f) == white && Gradient::Solid(red).Evaluate(0.7f) == red));
}

TEST_SUITE_END();
