// Tests for wlw::fx: ParticleSystem (spawning, lifetime, motion, capacity, instances), FlowParticles
// (paths, routing, queues, emitters, sprites), scene::SpriteBatch, scene::BillboardBatch and Shockwaves.
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/fx/flow_particles.h"
#include "wlw/fx/particle_system.h"
#include "wlw/fx/shockwaves.h"
#include "wlw/scene/billboard_batch.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/node.h"
#include "wlw/scene/sprite_batch.h"

TEST_SUITE_BEGIN("fx");

using wlw::fx::BurstSettings;
using wlw::fx::FlowParticles;
using wlw::fx::ParticleSystem;

static BurstSettings Fixed(int count) {
  BurstSettings s;
  s.count_min = s.count_max = count;
  return s;
}

TEST_CASE("burst count and capacity") {
  ParticleSystem ps(50);
  BurstSettings s;
  s.count_min = 8;
  s.count_max = 12;
  for (int i = 0; i < 4; ++i) {
    const int made = ps.Burst(s, glm::vec3(0.0f));
    CHECK((made >= 8 && made <= 12));
  }
  CHECK((ps.Count() >= 32 && ps.Count() <= 48));
  for (int i = 0; i < 10; ++i) ps.Burst(s, glm::vec3(0.0f));
  CHECK(ps.Count() == 50);  // full: bursts are cut short, memory doesn't grow
  CHECK(ps.Burst(s, glm::vec3(0.0f)) == 0);
}

TEST_CASE("particles die at their lifetime") {
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

TEST_CASE("gravity") {
  ParticleSystem ps;
  ps.gravity = glm::vec3(0.0f, 0.0f, -10.0f);
  ps.drag = 0.0f;
  BurstSettings s = Fixed(1);
  s.speed_min = s.speed_max = 0.0f;
  s.lifetime_min = s.lifetime_max = 10.0f;
  ps.Burst(s, glm::vec3(0.0f));
  for (int i = 0; i < 100; ++i) ps.Update(0.01f);  // 1 second
  // Exact free fall: z = -g t^2 / 2 = -5. Semi-implicit Euler lands within ~1% of that.
  CHECK(std::fabs(ps.Particles()[0].position.z + 5.0f) < 0.1f);
  CHECK(std::fabs(ps.Particles()[0].velocity.z + 10.0f) < 1e-3f);
}

TEST_CASE("default gravity pulls down -z") {
  const ParticleSystem ps;
  CHECK((ps.gravity.x == 0.0f && ps.gravity.y == 0.0f && ps.gravity.z < 0.0f));
  CHECK(BurstSettings{}.direction == wlw::kUp);
}

TEST_CASE("drag is frame rate independent") {
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

TEST_CASE("directions stay in the cone") {
  ParticleSystem ps;
  BurstSettings s = Fixed(200);
  s.direction = glm::vec3(0.0f, 0.0f, 2.0f);  // not unit length on purpose
  s.spread = 30.0f * wlw::kDegToRad;
  ps.Burst(s, glm::vec3(0.0f));
  const float cos_limit = std::cos(30.0f * wlw::kDegToRad) - 1e-4f;
  for (const auto& p : ps.Particles()) CHECK(glm::dot(glm::normalize(p.velocity), glm::vec3(0, 0, 1)) >= cos_limit);
}

TEST_CASE("delay and instances") {
  ParticleSystem ps;
  ps.gravity = glm::vec3(0.0f, -10.0f, 0.0f);
  BurstSettings s = Fixed(5);
  s.delay = 0.5f;
  s.size_start = 1.0f;
  s.size_end = 0.0f;
  s.lifetime_min = s.lifetime_max = 1.0f;
  ps.Burst(s, glm::vec3(1.0f, 2.0f, 3.0f));

  wlw::scene::MultiMeshInstance3D node;
  ps.Update(0.25f);
  ps.WriteTo(node);
  CHECK(node.InstanceCount() == 0);                                  // still delayed: invisible...
  CHECK(ps.Particles()[0].position == glm::vec3(1.0f, 2.0f, 3.0f));  // ...and not moving

  ps.Update(0.25f);  // delay over
  ps.Update(0.5f);   // half its life
  ps.WriteTo(node);
  REQUIRE(node.InstanceCount() == 5);
  CHECK(Near(ps.Particles()[0].Size(), 0.5f, 1e-3f));  // shrinking linearly
  // The instance transform carries the position (column 3) and the size (column lengths).
  const glm::mat4 first = node.Instances()[0].Transform();
  CHECK(glm::length(glm::vec3(first[3]) - ps.Particles()[0].position) < 1e-4f);
  CHECK(Near(glm::length(glm::vec3(first[0])), 0.5f, 1e-3f));
}

TEST_CASE("write to matches the matrices glm builds") {
  // WriteTo writes T * Rx * Ry * Rz * S in closed form with core::SinCos: the same copies as glm's
  // translate / rotate / scale (what BuildInstances did before 1.6), the same colours as Color32::Packed.
  ParticleSystem ps(300, 5u);
  BurstSettings s = Fixed(100);
  s.spread = wlw::kPi;
  s.max_spin = 35.0f;  // radians per second
  s.lifetime_min = s.lifetime_max = 10.0f;
  s.size_start = 2.0f;
  s.size_end = 0.1f;
  s.brightness_jitter = 0.6f;
  for (int i = 0; i < 3; ++i) ps.Burst(s, glm::vec3(static_cast<float>(i) * 4.0f, 1.0f, -2.0f));
  for (int frame = 0; frame < 30; ++frame) ps.Update(1.0f / 30.0f);  // turns of up to ~35 radians

  wlw::scene::MultiMeshInstance3D node;
  ps.WriteTo(node);
  REQUIRE(node.InstanceCount() == ps.Count());
  float worst = 0.0f;
  for (size_t i = 0; i < ps.Count(); ++i) {
    const ParticleSystem::Particle& p = ps.Particles()[i];
    glm::mat4 m = glm::translate(glm::mat4(1.0f), p.position);
    m = glm::rotate(m, p.angles.x, glm::vec3(1, 0, 0));
    m = glm::rotate(m, p.angles.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, p.angles.z, glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(p.Size()));
    const glm::mat4 written = node.Instances()[i].Transform();
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) worst = std::max(worst, std::abs(written[c][r] - m[c][r]));
    }
    CHECK(node.Instances()[i].color == wlw::Color32(p.color).Packed());
  }
  CHECK(worst < 1e-5f);
}

TEST_CASE("emit exactly") {
  ParticleSystem system(2);
  ParticleSystem::Particle p;
  p.position = {1.0f, 2.0f, 3.0f};
  p.velocity = {0.0f, 0.0f, 0.0f};
  p.color = {0.2f, 0.4f, 0.6f, 1.0f};
  p.size_start = p.size_end = 0.5f;
  p.lifetime = 1.0f;
  CHECK((system.Emit(p) && system.Count() == 1));
  // Exactly as given: no randomness anywhere.
  const ParticleSystem::Particle& out = system.Particles()[0];
  CHECK((out.position == p.position && out.color.g == 0.4f && out.Size() == 0.5f));
  // Delayed with a negative age, like a burst.
  p.age = -0.5f;
  CHECK(system.Emit(p));
  CHECK((!system.Emit(p) && system.Count() == 2));  // full: refused, not grown
}

TEST_CASE("floor bounce and edge") {
  ParticleSystem system(4);
  system.drag = 0.0f;
  system.gravity = {0.0f, 0.0f, -10.0f};
  system.floor = ParticleSystem::Floor{0.0f, {-1.0f, -1.0f}, {1.0f, 1.0f}, 0.5f, 0.5f};
  ParticleSystem::Particle p;
  p.size_start = p.size_end = 0.2f;
  p.lifetime = 10.0f;
  // Dropped onto the floor from 1 unit up: it lands, bounces back up slower, and ends up resting
  // on top (its bottom on the floor), never below.
  p.position = {0.0f, 0.0f, 1.0f};
  p.velocity = {0.5f, 0.0f, 0.0f};  // and drifting sideways
  system.Emit(p);
  float lowest = 1e9f, peak_after_bounce = 0.0f;
  bool bounced = false;
  for (int i = 0; i < 600; ++i) {
    system.Update(1.0f / 120.0f);
    const ParticleSystem::Particle& q = system.Particles()[0];
    lowest = std::min(lowest, q.position.z);
    if (q.velocity.z > 0.0f) bounced = true;
    if (bounced) peak_after_bounce = std::max(peak_after_bounce, q.position.z);
    if (q.position.x > 0.9f) break;  // stay on the floor for this part
  }
  CHECK((bounced && lowest >= 0.1f - 1e-3f));                     // never sank in: the cube's half size above
  CHECK((peak_after_bounce > 0.1f && peak_after_bounce < 0.6f));  // lower than where it fell from
  // Sliding loses speed at each bounce (friction).
  CHECK(system.Particles()[0].velocity.x < 0.5f);

  // Past the edge there's no floor: a particle falls right through.
  ParticleSystem edge(1);
  edge.floor = system.floor;
  edge.drag = 0.0f;
  ParticleSystem::Particle off = p;
  off.position = {3.0f, 0.0f, 1.0f};
  off.velocity = {0.0f, 0.0f, 0.0f};
  edge.Emit(off);
  for (int i = 0; i < 120; ++i) edge.Update(1.0f / 60.0f);
  CHECK(edge.Particles()[0].position.z < -1.0f);
  // And one that fell below, moving back over the floor, doesn't pop up through it.
  ParticleSystem under(1);
  under.floor = system.floor;
  ParticleSystem::Particle below = p;
  below.position = {0.0f, 0.0f, -0.5f};
  below.velocity = {0.0f, 0.0f, 0.0f};
  under.Emit(below);
  under.Update(1.0f / 60.0f);
  CHECK(under.Particles()[0].position.z < -0.5f);
}

// ---- SpriteBatch: the 16-byte dots ----

TEST_CASE("sprite batch") {
  CHECK(sizeof(wlw::rendering::SpriteInstance) == 16);
  const uint32_t red = wlw::Color32(wlw::Color{1.0f, 0.0f, 0.5f, 1.0f}).Packed();
  CHECK(((red & 0xFFu) == 255 && ((red >> 8) & 0xFFu) == 0 && ((red >> 16) & 0xFFu) == 128 && (red >> 24) == 255));
  CHECK((Near(wlw::Color32::FromPacked(red).ToColor().b, 128.0f / 255.0f) &&
         wlw::Color32(wlw::Color{2.0f, -1.0f, 0.0f, 0.0f}).Packed() == 0xFFu));  // clamped

  wlw::scene::SpriteBatch batch(3);
  std::shared_ptr<wlw::scene::Node3D> as_node = std::make_shared<wlw::scene::SpriteBatch>(1);
  CHECK((as_node->Kind() == wlw::scene::NodeKind::SpriteBatch &&
         wlw::scene::Node3D().Kind() == wlw::scene::NodeKind::Node));
  CHECK((batch.GetMaterial() && batch.GetMaterial()->blend == wlw::rendering::BlendMode::Additive));
  const uint64_t before = batch.Internal().Version();
  auto out = batch.Write(5);
  CHECK((out.size() == 3 && batch.Count() == 3 && batch.Internal().Version() == before + 1));  // cut at the capacity
  out[0] = {{0.0f, 0.0f}, 2.0f, red};
  out[1] = {{4.0f, 1.0f}, 1.0f, red};
  out[2] = {{1.0f, 0.0f}, 0.5f, red};
  batch.FitBounds();
  CHECK((batch.BoundsMin() == glm::vec2(-1.0f, -1.0f) && batch.BoundsMax() == glm::vec2(4.5f, 1.5f)));
  batch.SetPosition({10.0f, 0.0f, 2.0f});
  const wlw::AABB world = batch.WorldBounds();
  CHECK((Near(world.min.x, 9.0f) && Near(world.max.x, 14.5f) && Near(world.min.z, 2.0f) && Near(world.max.z, 2.0f)));
  batch.Clear();
  CHECK((batch.Empty() && batch.Internal().Version() == before + 2));
}

// ---- BillboardBatch: the 40-byte camera-facing quads ----

TEST_CASE("billboard batch") {
  using wlw::rendering::BillboardInstance;
  CHECK(sizeof(BillboardInstance) == 40);
  wlw::scene::BillboardBatch batch(2);
  CHECK((batch.Kind() == wlw::scene::NodeKind::BillboardBatch && batch.GetMaterial() &&
         batch.GetMaterial()->blend == wlw::rendering::BlendMode::Additive));
  const uint64_t before = batch.Internal().Version();
  auto out = batch.Write(5);
  CHECK((out.size() == 2 && batch.Count() == 2 && batch.Internal().Version() == before + 1));  // cut at the capacity
  out[0] = {.position = {0.0f, 0.0f, 1.0f}, .size = 2.0f};
  out[1] = {.position = {4.0f, 0.0f, 0.0f}, .size = 1.0f, .stretch = {0.0f, -3.0f, 1.0f}};
  batch.FitBounds();
  // The round one reaches half its size every way; the streak half its stretch more per axis.
  CHECK((batch.Bounds().min == glm::vec3(-1.0f, -2.0f, -1.0f) && batch.Bounds().max == glm::vec3(4.5f, 2.0f, 2.0f)));
  batch.SetPosition({10.0f, 0.0f, 0.0f});
  batch.SetScale(glm::vec3(2.0f));
  const wlw::AABB world = batch.WorldBounds();
  CHECK((Near(world.min.x, 8.0f) && Near(world.max.x, 19.0f) && Near(world.max.z, 4.0f)));
  CHECK((wlw::rendering::BillboardBounds({}).min == glm::vec3(0.0f) &&
         wlw::rendering::BillboardBounds({}).max == glm::vec3(0.0f)));
  batch.Clear();
  CHECK((batch.Empty() && batch.Internal().Version() == before + 2));
}

TEST_CASE("particles to billboards: over-life settings, stretch, bounds") {
  ParticleSystem system(8);
  system.gravity = glm::vec3(0.0f);
  system.drag = 0.0f;
  ParticleSystem::Particle p;
  p.velocity = {10.0f, 0.0f, 0.0f};
  p.size_start = p.size_end = 2.0f;
  p.color = {1.0f, 0.5f, 0.0f, 1.0f};
  p.intensity = 3.0f;
  p.angles = {0.0f, 0.0f, 0.7f};
  p.lifetime = 1.0f;
  p.age = 0.5f;
  system.Emit(p);
  ParticleSystem::Particle delayed = p;
  delayed.age = -0.1f;
  system.Emit(delayed);
  ParticleSystem::Particle gone = p;
  gone.size_start = gone.size_end = 0.0f;
  system.Emit(gone);

  wlw::scene::BillboardBatch batch(8);
  system.WriteTo(batch);
  REQUIRE(batch.Count() == 1);  // the delayed and the zero-sized ones aren't drawn
  const wlw::rendering::BillboardInstance& b = batch.Billboards()[0];
  CHECK((Near(b.size, 2.0f) && b.stretch == glm::vec3(0.0f) && Near(b.rotation, 0.7f) && Near(b.intensity, 3.0f)));
  CHECK(b.color == wlw::Color32(wlw::Color{1.0f, 0.5f, 0.0f, 1.0f}).Packed());

  // Over life at t = 0.5: size x 0.5 (a line 1 -> 0), colour x a gradient's middle, intensity x 2.
  system.size_over_life = wlw::anim::Curve::Linear({{0.0f, 1.0f}, {1.0f, 0.0f}});
  system.color_over_life = wlw::anim::Gradient({{0.0f, {1, 1, 1, 1}}, {1.0f, {1, 1, 1, 0}}});
  system.intensity_over_life = wlw::anim::Curve::Constant(2.0f);
  system.stretch = {.velocity_scale = 0.05f, .max_length = 1e9f};
  system.WriteTo(batch);
  const wlw::rendering::BillboardInstance& c = batch.Billboards()[0];
  CHECK((Near(c.size, 1.0f, 0.01f) && Near(c.intensity, 6.0f) && Near(c.stretch.x, 0.5f)));
  CHECK(Near(wlw::Color32::FromPacked(c.color).ToColor().a, 0.5f, 0.01f));
  CHECK((Near(batch.Bounds().min.x, -0.75f, 0.01f) && Near(batch.Bounds().max.x, 0.75f, 0.01f)));  // 0.25 + 0.5
  system.stretch.max_length = 0.2f;                                                                // capped
  system.WriteTo(batch);
  CHECK(Near(glm::length(batch.Billboards()[0].stretch), 0.2f));

  // The same settings shape the mesh copies (size, colour; meshes have no intensity).
  wlw::scene::MultiMeshInstance3D node;
  system.WriteTo(node);
  REQUIRE(node.InstanceCount() == 1);
  CHECK(Near(glm::length(glm::vec3(node.Instances()[0].rows[0])), 1.0f, 0.01f));
  CHECK((Near(system.SizeOf(system.Particles()[0]), 1.0f, 0.01f) &&
         Near(system.IntensityOf(system.Particles()[0]), 6.0f)));

  // A batch smaller than the system takes what fits.
  for (int i = 0; i < 4; ++i) system.Emit(p);
  wlw::scene::BillboardBatch small(2);
  system.WriteTo(small);
  CHECK(small.Count() == 2);
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

TEST_CASE("flow path geometry") {
  FlowParticles flow;
  const std::vector<glm::vec2> corner = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
  const int path = flow.AddPath(corner, {.width = 0.2f});
  CHECK((path == 0 && flow.PathCount() == 1));
  CHECK(Near(flow.PathLength(path), 2.0f));
  CHECK(glm::length(flow.PointAt(path, 0.5f) - glm::vec2(0.5f, 0.0f)) < 1e-4f);
  CHECK(glm::length(flow.PointAt(path, 1.5f) - glm::vec2(1.0f, 0.5f)) < 1e-4f);  // round the corner
  CHECK(glm::length(flow.PointAt(path, 9.0f) - glm::vec2(1.0f, 1.0f)) < 1e-4f);  // clamped to the end
  CHECK(glm::length(flow.PointAt(path, -1.0f)) < 1e-4f);
}

TEST_CASE("flow moves at its speed") {
  FlowParticles flow = Exact();
  const int path = Line(flow, 10.0f, 2.0f);
  CHECK(flow.Emit(path, 1.0f, {1, 1, 1, 1}));
  flow.Update(0.5f);
  CHECK((Near(flow.GetDot(0).distance, 2.0f) && flow.GetDot(0).place == -1));
  flow.SetSpeed(path, 0.0f);
  flow.Update(1.0f);
  CHECK(Near(flow.GetDot(0).distance, 2.0f));  // speed 0: still
}

TEST_CASE("flow routing and dead ends") {
  FlowParticles flow = Exact();
  const int a = Line(flow, 1.0f, 1.0f), b = Line(flow, 1.0f, 1.0f), c = Line(flow, 1.0f, 1.0f);
  const std::vector<int> next = {b, c};
  flow.SetNext(a, next);
  flow.Emit(a, 0.9f, {1, 1, 1, 1});
  flow.Emit(a, 0.9f, {1, 1, 1, 1});
  flow.Update(0.2f);
  CHECK(flow.Count() == 2);
  CHECK((flow.GetDot(0).path == b && flow.GetDot(1).path == c));                        // in turn
  CHECK((Near(flow.GetDot(0).distance, 0.1f) && Near(flow.GetDot(1).distance, 0.1f)));  // the overshoot is kept
  flow.Update(1.0f);
  CHECK(flow.Count() == 0);  // b and c lead nowhere: removed at their ends
}

TEST_CASE("flow queues at a stop") {
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
  CHECK((flow.Queued(path) == 4 && Near(flow.GetDot(3).distance, 4.7f)));

  flow.SetStop(path, FlowParticles::kOpen);  // open the gate: everyone moves on
  CHECK((flow.Queued(path) == 0 && flow.GetDot(0).place == -1));
  flow.Update(0.1f);
  CHECK((flow.GetDot(0).distance > 5.0f && Near(flow.GetDot(3).distance, 5.2f)));
}

TEST_CASE("flow emitters and capacity") {
  FlowParticles flow = Exact(100);
  const int path = Line(flow, 1000.0f, 1.0f);
  flow.SetEmitter(path, 60.0f, {0, 1, 0, 1});
  for (int i = 0; i < 60; ++i) flow.Update(1.0f / 60.0f);
  CHECK((flow.Count() >= 59 && flow.Count() <= 61));  // about the rate x the time
  CHECK(flow.GetDot(0).color == wlw::Color32(wlw::Color{0, 1, 0, 1}).Packed());
  flow.SetEmitter(path, 100000.0f, {0, 1, 0, 1});
  flow.Update(0.1f);
  CHECK((flow.Count() == 100 && !flow.Emit(path, 0.0f, {1, 1, 1, 1})));  // full: no growth
  flow.Clear();
  CHECK(flow.Count() == 0);
}

TEST_CASE("flow writes sprites") {
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
  CHECK((inside_width && inside_bounds));

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
  CHECK((Near(small.Sprites()[0].size, 0.0f) && Near(small.Sprites()[1].size, ends.dot_size)));
}
TEST_CASE("flow density") {
  // Emitters spawn the density's share of their rate.
  FlowParticles flow = Exact();
  const int pipe = Line(flow, 1000.0f, 1.0f);
  flow.SetDensity(0.5f);
  flow.SetEmitter(pipe, 60.0f, {1, 1, 1, 1});
  for (int i = 0; i < 60; ++i) flow.Update(1.0f / 60.0f);
  CHECK((flow.Count() >= 29 && flow.Count() <= 31));
  flow.SetDensity(5.0f);
  CHECK(flow.Density() == 1.0f);  // clamped to 0.01..1

  // Queues pack the density's share per unit, so a blocked pipe fills at the same speed; dots that
  // queued at another density keep their places (no gap in the line).
  FlowParticles queue = Exact();
  const int path = Line(queue, 10.0f, 5.0f, /*packed=*/10.0f);
  queue.SetStop(path, 5.0f);
  queue.Emit(path, 0.0f, {1, 1, 1, 1});
  queue.Emit(path, 0.0f, {1, 1, 1, 1});
  queue.Update(2.0f);
  CHECK(Near(queue.GetDot(1).distance, 4.9f));
  queue.SetDensity(0.5f);
  queue.Emit(path, 0.0f, {1, 1, 1, 1});
  queue.Emit(path, 0.0f, {1, 1, 1, 1});
  queue.Update(2.0f);
  CHECK(queue.Queued(path) == 4);
  CHECK((Near(queue.GetDot(2).distance, 4.8f) && Near(queue.GetDot(3).distance, 4.6f)));  // 0.2 apart now
  queue.SetStop(path, FlowParticles::kOpen);  // the line's length goes with it
  queue.SetStop(path, 9.0f);
  queue.Update(2.0f);
  float front = 0.0f, back = 100.0f;
  for (size_t i = 0; i < 4; ++i) {
    front = std::max(front, queue.GetDot(i).distance);
    back = std::min(back, queue.GetDot(i).distance);
  }
  CHECK((queue.Queued(path) == 4 && Near(front, 9.0f) && Near(back, 8.4f)));  // 4 dots at 0.2 apart
}

TEST_CASE("shockwave life") {
  const wlw::fx::ShockwaveSettings wave{.duration = 1.0f,
                                        .delay = 0.5f,
                                        .start_radius = 0.1f,
                                        .end_radius = 0.5f,
                                        .width = 0.1f,
                                        .end_width = 0.3f,
                                        .strength = 0.02f,
                                        .chroma = 0.3f};
  const glm::vec2 center{0.25f, 0.75f};
  // Off while delayed and from its end on.
  CHECK(!wlw::fx::ShockwaveAt(center, wave, 0.4f).Active());
  CHECK(!wlw::fx::ShockwaveAt(center, wave, 1.5f).Active());
  // Halfway: the radius is OutCubic's 7/8 of the way, the width half way, the push faded by (1 - 0.5)^2.
  const wlw::rendering::Ripple half = wlw::fx::ShockwaveAt(center, wave, 1.0f);
  CHECK((half.center == center && Near(half.radius, 0.1f + 0.4f * 0.875f) && Near(half.width, 0.2f)));
  CHECK((Near(half.strength, 0.02f * 0.25f) && half.chroma == 0.3f));
  // It fades in (no pop on the first frame), peaks early, then only weakens.
  const float first = wlw::fx::ShockwaveAt(center, wave, 0.51f).strength;
  const float peak = wlw::fx::ShockwaveAt(center, wave, 0.6f).strength;
  CHECK((first > 0.0f && first < 0.5f * peak && peak > half.strength));
  float previous = peak;
  for (float age = 0.65f; age < 1.5f; age += 0.05f) {
    const float strength = wlw::fx::ShockwaveAt(center, wave, age).strength;
    CHECK(strength <= previous);
    previous = strength;
  }
}

TEST_CASE("shockwaves into the post settings") {
  wlw::fx::Shockwaves waves;
  wlw::rendering::PostSettings post;
  post.vignette.intensity = 0.3f;
  waves.WriteTo(post);
  CHECK((!waves.Active() && !post.Distorts()));

  // A wave is a ripple until its end; WriteTo leaves the other settings alone.
  waves.Add({0.5f, 0.5f}, {.duration = 0.5f});
  waves.Update(0.1f);
  waves.WriteTo(post);
  CHECK((waves.Count() == 1 && post.ripples[0].Active() && post.Distorts() && post.vignette.intensity == 0.3f));
  waves.Update(0.4f);
  waves.WriteTo(post);
  CHECK((!waves.Active() && !post.ripples[0].Active() && !post.Distorts()));

  // Ages in steps of any size land on the same ring.
  wlw::fx::Shockwaves fine, coarse;
  fine.Add({0.5f, 0.5f});
  coarse.Add({0.5f, 0.5f});
  for (int i = 0; i < 30; ++i) fine.Update(0.01f);
  coarse.Update(0.3f);
  wlw::rendering::PostSettings a, b;
  fine.WriteTo(a);
  coarse.WriteTo(b);
  CHECK((Near(a.ripples[0].radius, b.ripples[0].radius) && Near(a.ripples[0].strength, b.ripples[0].strength)));

  // All slots busy: a new wave takes the place of the one nearest its end (the first, the oldest here).
  for (int i = 0; i < wlw::rendering::kMaxRipples; ++i) {
    waves.Add({0.1f * static_cast<float>(i), 0.5f});
    waves.Update(0.05f);
  }
  waves.Add({0.9f, 0.9f});
  waves.Update(0.01f);
  waves.WriteTo(post);
  CHECK(waves.Count() == wlw::rendering::kMaxRipples);
  CHECK((post.ripples[0].center == glm::vec2(0.9f, 0.9f) && post.ripples[1].center == glm::vec2(0.1f, 0.5f)));
  waves.Clear();
  CHECK(!waves.Active());
}

TEST_SUITE_END();
