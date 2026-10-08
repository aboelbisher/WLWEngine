// Tests for wlw::fx::ParticleSystem: spawning, lifetime, motion, capacity, instances.
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

int main() {
  TestBurstCountAndCapacity();
  TestParticlesDieAtTheirLifetime();
  TestGravity();
  TestDragIsFrameRateIndependent();
  TestDirectionsStayInTheCone();
  TestDelayAndInstances();
  return TestResult("fx");
}
