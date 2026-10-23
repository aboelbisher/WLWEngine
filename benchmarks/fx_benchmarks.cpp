// Particles: FlowParticles at 20k dots (Update, WriteTo) and ParticleSystem at 5k (Update,
// WriteTo into a node's instances): the sizes Bluzzle's effects run at.
#include <cmath>
#include <vector>

#include "bench.h"
#include "wlw/fx/flow_particles.h"
#include "wlw/fx/particle_system.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/sprite_batch.h"

using namespace wlw;

namespace {

// 8 winding paths, each feeding itself (dots wrap round), filled with 20k dots; one has a stop, so
// a queue is part of the work, as in a game with a closed gate.
void FlowBenchmarks(bench::Context& ctx) {
  constexpr size_t kDots = 20000;
  fx::FlowParticles flow(kDots, 1234u);
  for (int p = 0; p < 8; ++p) {
    std::vector<glm::vec2> points;
    for (int i = 0; i <= 20; ++i) {
      const float x = static_cast<float>(i) * 0.5f;
      points.push_back({x, static_cast<float>(p) * 1.5f + std::sin(x * 0.7f + static_cast<float>(p))});
    }
    const int path = flow.AddPath(points);
    const int self[] = {path};
    flow.SetNext(path, self);
  }
  flow.SetStop(0, flow.PathLength(0) * 0.6f);
  for (size_t i = 0; i < kDots; ++i) {
    const int path = static_cast<int>(i % 8);
    const float distance = flow.PathLength(path) * static_cast<float>(i / 8) / static_cast<float>(kDots / 8);
    flow.Emit(path, distance, Color{0.2f, 0.6f, 1.0f, 1.0f});
  }
  for (int i = 0; i < 120; ++i) flow.Update(1.0f / 60.0f);  // queues formed: a steady state

  ctx.Run("fx/flow update 20k", [&] {
    flow.Update(1.0f / 60.0f);
    bench::Context::Keep(flow.Count());
  });
  scene::SpriteBatch batch(kDots);
  ctx.Run("fx/flow write 20k", [&] {
    flow.WriteTo(batch);
    bench::Context::Keep(batch.Count());
  });
}

// 5k particles that live long enough to stay 5k for the whole run, with a floor to bounce on.
void ParticleBenchmarks(bench::Context& ctx) {
  constexpr int kParticles = 5000;
  fx::ParticleSystem system(kParticles, 99u);
  fx::BurstSettings burst;
  burst.count_min = burst.count_max = 100;
  burst.lifetime_min = burst.lifetime_max = 1e9f;
  burst.spread_degrees = 70.0f;
  burst.position_jitter = 0.5f;
  for (int i = 0; i < kParticles / 100; ++i) system.Burst(burst, glm::vec3(static_cast<float>(i % 10), 0.0f, 0.0f));
  system.floor = fx::ParticleSystem::Floor{.height = -1.0f, .min = {-20.0f, -20.0f}, .max = {20.0f, 20.0f}};

  ctx.Run("fx/particles update 5k", [&] {
    system.Update(1.0f / 60.0f);
    bench::Context::Keep(system.Count());
  });
  scene::MultiMeshInstance3D node;  // (the name is older than WriteTo: before 1.6 it timed BuildInstances)
  ctx.Run("fx/particles instances 5k", [&] {
    system.WriteTo(node);
    bench::Context::Keep(node.InstanceCount());
  });
}

}  // namespace

WLW_BENCHMARK("fx") {
  FlowBenchmarks(ctx);
  ParticleBenchmarks(ctx);
}
