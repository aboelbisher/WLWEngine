// Physics (wlw::physics on Jolt): Snapfold's scale, 300 magnet tiles and 400 hinges on a table, everything awake
// (sleeping off: the worst case, e.g. the table shaking). One op = one fixed step (1/120 s; a 60 fps frame runs two),
// the hinge readback a game does every step (breaking joints), and the ray casts of a few fingers.
// The tiles, solver and joints use Snapfold's numbers (tile edge = 1, gravity 130, its G0 feel test).
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "bench.h"
#include "wlw/physics/world.h"

using namespace wlw;
using namespace wlw::physics;

namespace {

constexpr float kThickness = 0.09f;
constexpr float kHalf = 0.5f * kThickness;
constexpr float kGravity = 130.0f;
constexpr float kStep = 1.0f / 120.0f;
constexpr Layer kTable = 0;
constexpr Layer kTiles = 1;

// A unit square tile in its XY plane, corners cut by 0.12 (as Snapfold's are), thickness along Z.
Shape TileShape() {
  std::vector<Vec3> points;
  constexpr float c = 0.5f;
  constexpr float cut = 0.12f;
  const Vec2 outline[] = {{-c + cut, -c}, {c - cut, -c}, {c, -c + cut}, {c, c - cut},
                          {c - cut, c},   {-c + cut, c}, {-c, c - cut}, {-c, -c + cut}};
  for (float z : {-kHalf, kHalf}) {
    for (Vec2 p : outline) {
      points.push_back({p.x, p.y, z});
    }
  }
  return Shape::ConvexHull(points, 0.02f);
}

struct Scene {
  std::unique_ptr<World> world;
  std::vector<BodyId> tiles;
  std::vector<JointId> hinges;
};

Scene MakeScene(int worker_threads) {
  WorldSettings settings;
  settings.layers = CollisionLayers(2);
  settings.layers.SetCollides(kTable, kTable, false);
  settings.gravity = {0.0f, 0.0f, -kGravity};
  settings.solver = {.velocity_steps = 12,
                     .position_steps = 4,
                     .penetration_slop = 0.004f,
                     .speculative_distance = 0.012f,
                     .allow_sleeping = false};
  settings.worker_threads = worker_threads;
  Scene scene;
  scene.world = World::Create(settings);
  World& world = *scene.world;
  world.CreateBody({.shape = Shape::Box({30.0f, 30.0f, 1.0f}),
                    .position = {9.0f, 6.0f, -1.0f},
                    .motion = MotionType::Static,
                    .layer = kTable,
                    .friction = 0.7f});

  const Shape tile = TileShape();
  auto add_tile = [&](Vec3 position, Quat rotation) {
    const BodyId id = world.CreateBody({.shape = tile,
                                        .position = position,
                                        .rotation = rotation,
                                        .layer = kTiles,
                                        .mass = 1.0f,
                                        .friction = 0.7f,
                                        .restitution = 0.04f,
                                        .linear_damping = 0.05f,
                                        .angular_damping = 0.15f,
                                        .continuous = true,
                                        .allow_sleeping = false});
    scene.tiles.push_back(id);
    return id;
  };
  auto add_hinge = [&](BodyId a, BodyId b, Vec3 point, Vec3 axis, Vec3 normal_a, Vec3 normal_b) {
    scene.hinges.push_back(world.AddHinge({.body_a = a,
                                           .body_b = b,
                                           .point = point,
                                           .axis = axis,
                                           .normal_a = normal_a,
                                           .normal_b = normal_b,
                                           .min_angle = -166.0f * kDegToRad,
                                           .max_angle = 166.0f * kDegToRad,
                                           .friction_torque = 0.06f * kGravity}));
  };

  // 50 open boxes (a floor and 4 walls, 8 hinges each) in a 10 x 5 grid, a loose tile next to each.
  const Quat flat(1.0f, 0.0f, 0.0f, 0.0f);
  for (int i = 0; i < 50; ++i) {
    const float cx = 2.0f * static_cast<float>(i % 10);
    const float cy = 2.5f * static_cast<float>(i / 10);
    const float wall_z = kHalf + 0.5f;
    const BodyId floor = add_tile({cx, cy, kHalf}, flat);
    const BodyId east = add_tile({cx + 0.5f, cy, wall_z}, glm::angleAxis(kHalfPi, kForward));
    const BodyId north = add_tile({cx, cy + 0.5f, wall_z}, glm::angleAxis(-kHalfPi, kRight));
    const BodyId west = add_tile({cx - 0.5f, cy, wall_z}, glm::angleAxis(-kHalfPi, kForward));
    const BodyId south = add_tile({cx, cy - 0.5f, wall_z}, glm::angleAxis(kHalfPi, kRight));
    // Floor edges: the floor's outward normal, the wall's inward one (up).
    add_hinge(floor, east, {cx + 0.5f, cy, kHalf}, kForward, kRight, kUp);
    add_hinge(floor, north, {cx, cy + 0.5f, kHalf}, kRight, kForward, kUp);
    add_hinge(floor, west, {cx - 0.5f, cy, kHalf}, kForward, kLeft, kUp);
    add_hinge(floor, south, {cx, cy - 0.5f, kHalf}, kRight, kBack, kUp);
    // Corners: vertical axes.
    add_hinge(east, north, {cx + 0.5f, cy + 0.5f, wall_z}, kUp, kForward, kLeft);
    add_hinge(north, west, {cx - 0.5f, cy + 0.5f, wall_z}, kUp, kLeft, kBack);
    add_hinge(west, south, {cx - 0.5f, cy - 0.5f, wall_z}, kUp, kBack, kRight);
    add_hinge(south, east, {cx + 0.5f, cy - 0.5f, wall_z}, kUp, kRight, kForward);
    add_tile({cx + 1.0f, cy + 1.25f, kHalf}, glm::angleAxis(0.3f * static_cast<float>(i), kUp));
  }
  world.OptimizeBroadPhase();
  for (int i = 0; i < 240; ++i) {
    world.Step(kStep);  // 2 s: settled, contacts made
  }
  return scene;
}

void PhysicsBenchmarks(bench::Context& ctx) {
  for (int threads : {0, 3}) {
    const std::string name =
        threads == 0 ? "physics/step 300 tiles 400 hinges" : "physics/step 300 tiles 400 hinges, 3 workers";
    if (!ctx.Wants(name)) {
      continue;
    }
    Scene scene = MakeScene(threads);
    ctx.Run(name, [&] {
      scene.world->Step(kStep);
      bench::Context::Keep(scene.world->Position(scene.tiles[7]));
    });
  }

  if (!ctx.Wants("physics/hinge readback 400") && !ctx.Wants("physics/ray x10")) {
    return;
  }
  Scene scene = MakeScene(0);
  ctx.Run("physics/hinge readback 400", [&] {
    float most = 0.0f;
    for (JointId hinge : scene.hinges) {
      const HingeState state = scene.world->GetHinge(hinge);
      most = std::max(most, glm::length(state.force) + state.torque);
    }
    bench::Context::Keep(most);
  });
  ctx.Run("physics/ray x10", [&] {
    float total = 0.0f;
    for (int i = 0; i < 10; ++i) {
      const Vec3 origin{1.7f * static_cast<float>(i), 4.0f, 8.0f};
      if (const auto hit = scene.world->CastRay(origin, Vec3(0.1f, 0.3f, -1.0f), 50.0f)) {
        total += hit->distance;
      }
    }
    bench::Context::Keep(total);
  });
}

}  // namespace

WLW_BENCHMARK("physics") { PhysicsBenchmarks(ctx); }
