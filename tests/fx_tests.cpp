// Tests for wlw::fx::ParticleSystem: spawning, lifetime, motion, capacity, instances.
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "check.h"
#include "wlw/fx/particle_system.h"

using wlw::fx::BurstSettings;
using wlw::fx::ParticleSystem;

static BurstSettings Fixed(int count) {
  BurstSettings s;
  s.count_min = s.count_max = count;
  return s;
}

static void TestBurstCountAndCapacity() {
  ParticleSystem ps(50);
  BurstSettings s;
  s.count_min = 8;
  s.count_max = 12;
  for (int i = 0; i < 4; ++i) {
    const int made = ps.Burst(s, glm::vec3(0.0f));
    CHECK(made >= 8 && made <= 12);
  }
  CHECK(ps.Count() >= 32 && ps.Count() <= 48);
  for (int i = 0; i < 10; ++i) ps.Burst(s, glm::vec3(0.0f));
  CHECK(ps.Count() == 50);  // full: bursts are cut short, memory doesn't grow
  CHECK(ps.Burst(s, glm::vec3(0.0f)) == 0);
}

static void TestParticlesDieAtTheirLifetime() {
  ParticleSystem ps;
  BurstSettings s = Fixed(20);
  s.lifetime_min = 0.5f;
  s.lifetime_max = 1.0f;
  ps.Burst(s, glm::vec3(0.0f));
  for (int i = 0; i < 24; ++i) ps.Update(1.0f / 60.0f);  // 0.4 s: nobody is dead yet
  CHECK(ps.Count() == 20);
  for (int i = 0; i < 40; ++i) ps.Update(1.0f / 60.0f);  // 1.07 s: everybody is
  CHECK(ps.Count() == 0);
}

static void TestGravity() {
  ParticleSystem ps;
  ps.gravity = glm::vec3(0.0f, -10.0f, 0.0f);
  ps.drag = 0.0f;
  BurstSettings s = Fixed(1);
  s.speed_min = s.speed_max = 0.0f;
  s.lifetime_min = s.lifetime_max = 10.0f;
  ps.Burst(s, glm::vec3(0.0f));
  for (int i = 0; i < 100; ++i) ps.Update(0.01f);  // 1 second
  // Exact free fall: y = -g t^2 / 2 = -5. Semi-implicit Euler lands within ~1% of that.
  CHECK(std::fabs(ps.Particles()[0].position.y + 5.0f) < 0.1f);
  CHECK(std::fabs(ps.Particles()[0].velocity.y + 10.0f) < 1e-3f);
}

static void TestDragIsFrameRateIndependent() {
  auto speed_after_one_second = [](float dt) {
    ParticleSystem ps;
    ps.gravity = glm::vec3(0.0f);
    ps.drag = 2.0f;
    BurstSettings s = Fixed(1);
    s.speed_min = s.speed_max = 10.0f;
    s.lifetime_min = s.lifetime_max = 10.0f;
    ps.Burst(s, glm::vec3(0.0f));
    for (int i = 0; i < static_cast<int>(std::round(1.0f / dt)); ++i) ps.Update(dt);
    return glm::length(ps.Particles()[0].velocity);
  };
  const float at30 = speed_after_one_second(1.0f / 30.0f);
  const float at120 = speed_after_one_second(1.0f / 120.0f);
  CHECK(std::fabs(at30 - at120) < 1e-3f);
  CHECK(std::fabs(at30 - 10.0f * std::exp(-2.0f)) < 1e-3f);
}

static void TestDirectionsStayInTheCone() {
  ParticleSystem ps;
  BurstSettings s = Fixed(200);
  s.direction = glm::vec3(0.0f, 2.0f, 0.0f);  // not unit length on purpose
  s.spread_degrees = 30.0f;
  ps.Burst(s, glm::vec3(0.0f));
  const float cos_limit = std::cos(glm::radians(30.0f)) - 1e-4f;
  for (const auto& p : ps.Particles()) CHECK(glm::dot(glm::normalize(p.velocity), glm::vec3(0, 1, 0)) >= cos_limit);
}

static void TestDelayAndInstances() {
  ParticleSystem ps;
  ps.gravity = glm::vec3(0.0f, -10.0f, 0.0f);
  BurstSettings s = Fixed(5);
  s.delay = 0.5f;
  s.size_start = 1.0f;
  s.size_end = 0.0f;
  s.lifetime_min = s.lifetime_max = 1.0f;
  ps.Burst(s, glm::vec3(1.0f, 2.0f, 3.0f));

  std::vector<wlw::rendering::InstanceData> instances;
  ps.Update(0.25f);
  ps.BuildInstances(instances);
  CHECK(instances.empty());  // still delayed: invisible...
  CHECK(ps.Particles()[0].position == glm::vec3(1.0f, 2.0f, 3.0f));  // ...and not moving

  ps.Update(0.25f);  // delay over
  ps.Update(0.5f);   // half its life
  ps.BuildInstances(instances);
  CHECK(instances.size() == 5);
  CHECK(Near(ps.Particles()[0].Size(), 0.5f, 1e-3f));  // shrinking linearly
  // The instance transform carries the position (column 3) and the size (column lengths).
  CHECK(glm::length(glm::vec3(instances[0].transform[3]) - ps.Particles()[0].position) < 1e-4f);
  CHECK(Near(glm::length(glm::vec3(instances[0].transform[0])), 0.5f, 1e-3f));
}

static void TestEmitExactly() {
  ParticleSystem system(2);
  ParticleSystem::Particle p;
  p.position = {1.0f, 2.0f, 3.0f};
  p.velocity = {0.0f, 0.0f, 0.0f};
  p.color = {0.2f, 0.4f, 0.6f, 1.0f};
  p.size_start = p.size_end = 0.5f;
  p.lifetime = 1.0f;
  CHECK(system.Emit(p) && system.Count() == 1);
  // Exactly as given: no randomness anywhere.
  const ParticleSystem::Particle& out = system.Particles()[0];
  CHECK(out.position == p.position && out.color.g == 0.4f && out.Size() == 0.5f);
  // Delayed with a negative age, like a burst.
  p.age = -0.5f;
  CHECK(system.Emit(p));
  CHECK(!system.Emit(p) && system.Count() == 2);  // full: refused, not grown
}

static void TestFloorBounceAndEdge() {
  ParticleSystem system(4);
  system.drag = 0.0f;
  system.gravity = {0.0f, -10.0f, 0.0f};
  system.floor = ParticleSystem::Floor{0.0f, {-1.0f, -1.0f}, {1.0f, 1.0f}, 0.5f, 0.5f};
  ParticleSystem::Particle p;
  p.size_start = p.size_end = 0.2f;
  p.lifetime = 10.0f;
  // Dropped onto the floor from 1 unit up: it lands, bounces back up slower, and ends up resting
  // on top (its bottom on the floor), never below.
  p.position = {0.0f, 1.0f, 0.0f};
  p.velocity = {0.5f, 0.0f, 0.0f};  // and drifting sideways
  system.Emit(p);
  float lowest = 1e9f, peak_after_bounce = 0.0f;
  bool bounced = false;
  for (int i = 0; i < 600; ++i) {
    system.Update(1.0f / 120.0f);
    const ParticleSystem::Particle& q = system.Particles()[0];
    lowest = std::min(lowest, q.position.y);
    if (q.velocity.y > 0.0f) bounced = true;
    if (bounced) peak_after_bounce = std::max(peak_after_bounce, q.position.y);
    if (q.position.x > 0.9f) break;  // stay on the floor for this part
  }
  CHECK(bounced && lowest >= 0.1f - 1e-3f);           // never sank in: the cube's half size above
  CHECK(peak_after_bounce > 0.1f && peak_after_bounce < 0.6f);  // lower than where it fell from
  // Sliding loses speed at each bounce (friction).
  CHECK(system.Particles()[0].velocity.x < 0.5f);

  // Past the edge there's no floor: a particle falls right through.
  ParticleSystem edge(1);
  edge.floor = system.floor;
  edge.drag = 0.0f;
  ParticleSystem::Particle off = p;
  off.position = {3.0f, 1.0f, 0.0f};
  off.velocity = {0.0f, 0.0f, 0.0f};
  edge.Emit(off);
  for (int i = 0; i < 120; ++i) edge.Update(1.0f / 60.0f);
  CHECK(edge.Particles()[0].position.y < -1.0f);
  // And one that fell below, moving back over the floor, doesn't pop up through it.
  ParticleSystem under(1);
  under.floor = system.floor;
  ParticleSystem::Particle below = p;
  below.position = {0.0f, -0.5f, 0.0f};
  below.velocity = {0.0f, 0.0f, 0.0f};
  under.Emit(below);
  under.Update(1.0f / 60.0f);
  CHECK(under.Particles()[0].position.y < -0.5f);
}

int main() {
  TestBurstCountAndCapacity();
  TestParticlesDieAtTheirLifetime();
  TestGravity();
  TestDragIsFrameRateIndependent();
  TestDirectionsStayInTheCone();
  TestDelayAndInstances();
  TestEmitExactly();
  TestFloorBounceAndEdge();
  return TestResult("fx");
}
