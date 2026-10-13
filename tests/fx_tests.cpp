// Tests for wlw::fx: ParticleSystem (spawning, lifetime, motion, capacity, instances), FlowParticles
// (paths, routing, queues, emitters, sprites) and scene::SpriteBatch.
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "check.h"
#include "wlw/fx/flow_particles.h"
#include "wlw/fx/particle_system.h"
#include "wlw/scene/sprite_batch.h"

using wlw::fx::BurstSettings;
using wlw::fx::FlowParticles;
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

// ---- SpriteBatch: the 16-byte dots ----

static void TestSpriteBatch() {
  using wlw::rendering::PackColor;
  using wlw::rendering::UnpackColor;
  CHECK(sizeof(wlw::rendering::SpriteInstance) == 16);
  const uint32_t red = PackColor({1.0f, 0.0f, 0.5f, 1.0f});
  CHECK((red & 0xFFu) == 255 && ((red >> 8) & 0xFFu) == 0 && ((red >> 16) & 0xFFu) == 128 && (red >> 24) == 255);
  CHECK(Near(UnpackColor(red).b, 128.0f / 255.0f) && PackColor({2.0f, -1.0f, 0.0f, 0.0f}) == 0xFFu);  // clamped

  wlw::scene::SpriteBatch batch(3);
  std::shared_ptr<wlw::scene::Node3D> as_node = std::make_shared<wlw::scene::SpriteBatch>(1);
  CHECK(as_node->AsSpriteBatch() != nullptr && wlw::scene::Node3D().AsSpriteBatch() == nullptr);
  CHECK(batch.GetMaterial() && batch.GetMaterial()->blend == wlw::rendering::BlendMode::Additive);
  const uint64_t before = batch.Version();
  auto out = batch.Write(5);
  CHECK(out.size() == 3 && batch.Count() == 3 && batch.Version() == before + 1);  // cut at the capacity
  out[0] = {{0.0f, 0.0f}, 2.0f, red};
  out[1] = {{4.0f, 1.0f}, 1.0f, red};
  out[2] = {{1.0f, 0.0f}, 0.5f, red};
  batch.FitBounds();
  CHECK(batch.BoundsMin() == glm::vec2(-1.0f, -1.0f) && batch.BoundsMax() == glm::vec2(4.5f, 1.5f));
  batch.SetPosition({10.0f, 0.0f, 2.0f});
  const wlw::scene::AABB world = batch.WorldBounds();
  CHECK(Near(world.min.x, 9.0f) && Near(world.max.x, 14.5f) && Near(world.min.z, 2.0f) && Near(world.max.z, 2.0f));
  batch.Clear();
  CHECK(batch.Empty() && batch.Version() == before + 2);
}

// ---- FlowParticles: dots along paths ----

static FlowParticles Exact(size_t capacity = 1000) {
  FlowParticles flow(capacity);
  flow.speed_jitter = 0.0f;  // every dot at exactly the path's speed
  return flow;
}

static int Line(FlowParticles& flow, float length, float speed, float packed = 40.0f) {
  const std::vector<glm::vec2> points = {{0.0f, 0.0f}, {length, 0.0f}};
  return flow.AddPath(points, {.width = 0.2f, .speed = speed, .packed_per_unit = packed});
}

static void TestFlowPathGeometry() {
  FlowParticles flow;
  const std::vector<glm::vec2> corner = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
  const int path = flow.AddPath(corner, {.width = 0.2f});
  CHECK(path == 0 && flow.PathCount() == 1);
  CHECK(Near(flow.PathLength(path), 2.0f));
  CHECK(glm::length(flow.PointAt(path, 0.5f) - glm::vec2(0.5f, 0.0f)) < 1e-4f);
  CHECK(glm::length(flow.PointAt(path, 1.5f) - glm::vec2(1.0f, 0.5f)) < 1e-4f);  // round the corner
  CHECK(glm::length(flow.PointAt(path, 9.0f) - glm::vec2(1.0f, 1.0f)) < 1e-4f);  // clamped to the end
  CHECK(glm::length(flow.PointAt(path, -1.0f)) < 1e-4f);
}

static void TestFlowMovesAtItsSpeed() {
  FlowParticles flow = Exact();
  const int path = Line(flow, 10.0f, 2.0f);
  CHECK(flow.Emit(path, 1.0f, {1, 1, 1, 1}));
  flow.Update(0.5f);
  CHECK(Near(flow.GetDot(0).distance, 2.0f) && flow.GetDot(0).place == -1);
  flow.SetSpeed(path, 0.0f);
  flow.Update(1.0f);
  CHECK(Near(flow.GetDot(0).distance, 2.0f));  // speed 0: still
}

static void TestFlowRoutingAndDeadEnds() {
  FlowParticles flow = Exact();
  const int a = Line(flow, 1.0f, 1.0f), b = Line(flow, 1.0f, 1.0f), c = Line(flow, 1.0f, 1.0f);
  const std::vector<int> next = {b, c};
  flow.SetNext(a, next);
  flow.Emit(a, 0.9f, {1, 1, 1, 1});
  flow.Emit(a, 0.9f, {1, 1, 1, 1});
  flow.Update(0.2f);
  CHECK(flow.Count() == 2);
  CHECK(flow.GetDot(0).path == b && flow.GetDot(1).path == c);  // in turn
  CHECK(Near(flow.GetDot(0).distance, 0.1f) && Near(flow.GetDot(1).distance, 0.1f));  // the overshoot is kept
  flow.Update(1.0f);
  CHECK(flow.Count() == 0);  // b and c lead nowhere: removed at their ends
}

static void TestFlowQueuesAtAStop() {
  FlowParticles flow = Exact();
  const int path = Line(flow, 10.0f, 5.0f, /*packed=*/10.0f);
  flow.SetStop(path, 5.0f);
  for (int i = 0; i < 3; ++i) flow.Emit(path, 0.0f, {1, 1, 1, 1});
  flow.Update(2.0f);
  CHECK(flow.Queued(path) == 3);
  for (size_t i = 0; i < 3; ++i) {
    CHECK(flow.GetDot(i).place == static_cast<int>(i));
    CHECK(Near(flow.GetDot(i).distance, 5.0f - 0.1f * static_cast<float>(i)));  // the line grows backwards
  }
  flow.Emit(path, 0.0f, {1, 1, 1, 1});  // a latecomer joins at the back
  flow.Update(2.0f);
  CHECK(flow.Queued(path) == 4 && Near(flow.GetDot(3).distance, 4.7f));

  flow.SetStop(path, FlowParticles::kOpen);  // open the gate: everyone moves on
  CHECK(flow.Queued(path) == 0 && flow.GetDot(0).place == -1);
  flow.Update(0.1f);
  CHECK(flow.GetDot(0).distance > 5.0f && Near(flow.GetDot(3).distance, 5.2f));
}

static void TestFlowEmittersAndCapacity() {
  FlowParticles flow = Exact(100);
  const int path = Line(flow, 1000.0f, 1.0f);
  flow.SetEmitter(path, 60.0f, {0, 1, 0, 1});
  for (int i = 0; i < 60; ++i) flow.Update(1.0f / 60.0f);
  CHECK(flow.Count() >= 59 && flow.Count() <= 61);  // about the rate x the time
  CHECK(flow.GetDot(0).color == wlw::rendering::PackColor({0, 1, 0, 1}));
  flow.SetEmitter(path, 100000.0f, {0, 1, 0, 1});
  flow.Update(0.1f);
  CHECK(flow.Count() == 100 && !flow.Emit(path, 0.0f, {1, 1, 1, 1}));  // full: no growth
  flow.Clear();
  CHECK(flow.Count() == 0);
}

static void TestFlowWritesSprites() {
  FlowParticles flow(500);
  const std::vector<glm::vec2> points = {{0.0f, 0.0f}, {10.0f, 0.0f}};
  const int path = flow.AddPath(points, {.width = 0.4f, .wobble = 0.5f});
  wlw::core::Random random(7);
  for (int i = 0; i < 200; ++i) flow.Emit(path, random.Range(0.0f, 10.0f), {1, 0, 0, 1});
  flow.Update(0.3f);
  wlw::scene::SpriteBatch batch(500);
  flow.WriteTo(batch);
  CHECK(batch.Count() == flow.Count());
  bool inside_width = true, inside_bounds = true;
  for (const auto& dot : batch.Sprites()) {
    inside_width &= std::abs(dot.position.y) <= 0.2f + 1e-4f;
    inside_bounds &= dot.position.x - dot.size * 0.5f >= batch.BoundsMin().x - 1e-4f &&
                     dot.position.x + dot.size * 0.5f <= batch.BoundsMax().x + 1e-4f &&
                     dot.position.y - dot.size * 0.5f >= batch.BoundsMin().y - 1e-4f &&
                     dot.position.y + dot.size * 0.5f <= batch.BoundsMax().y + 1e-4f;
  }
  CHECK(inside_width && inside_bounds);

  // Dots grow in at the start of a path nothing feeds, and shrink out at a dead end.
  FlowParticles ends = Exact();
  const int line = Line(ends, 10.0f, 0.0f);
  ends.size_jitter = 0.0f;
  ends.Emit(line, 0.0f, {1, 1, 1, 1});
  ends.Emit(line, 5.0f, {1, 1, 1, 1});
  ends.Emit(line, 10.0f, {1, 1, 1, 1});
  wlw::scene::SpriteBatch small(2);
  ends.WriteTo(small);
  CHECK(small.Count() == 2);  // cut at the batch's capacity
  CHECK(Near(small.Sprites()[0].size, 0.0f) && Near(small.Sprites()[1].size, ends.dot_size));
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
  TestSpriteBatch();
  TestFlowPathGeometry();
  TestFlowMovesAtItsSpeed();
  TestFlowRoutingAndDeadEnds();
  TestFlowQueuesAtAStop();
  TestFlowEmittersAndCapacity();
  TestFlowWritesSprites();
  return TestResult("fx");
}
