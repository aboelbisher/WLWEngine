// Tests for wlw::physics (Jolt behind the engine's API): shapes, bodies falling and resting, layers, kinematic moves,
// forces, sleeping, motion changes, hinges (angle sign, limits, friction, force readback, no collision between joined
// bodies), ids after destroying, ray casts with filters, and the same results with worker threads.
#include <cmath>
#include <vector>

#include "test_helpers.h"
#include "wlw/core/color.h"
#include "wlw/core/geometry.h"
#include "wlw/debug/debug_draw.h"
#include "wlw/physics/world.h"

TEST_SUITE_BEGIN("physics");

using namespace wlw;
using namespace wlw::physics;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr Layer kStatic = 0;
constexpr Layer kMoving = 1;
constexpr Layer kGhost = 2;  // collides with nothing

WorldSettings TestSettings() {
  WorldSettings settings;
  settings.layers = CollisionLayers(3);
  settings.layers.SetCollides(kStatic, kStatic, false);
  settings.layers.SetCollidesWithNothing(kGhost);
  return settings;
}

void Run(World& world, float seconds, float dt = kStep) {
  for (float t = 0.0f; t < seconds; t += dt) {
    world.Step(dt);
  }
}

// A floor whose top is z = 0.
BodyId AddFloor(World& world) {
  return world.CreateBody({.shape = Shape::Box({20.0f, 20.0f, 0.5f}),
                           .position = {0.0f, 0.0f, -0.5f},
                           .motion = MotionType::Static,
                           .layer = kStatic});
}

Quat Turn(float radians, Vec3 axis) { return glm::angleAxis(radians, axis); }

}  // namespace

TEST_CASE("shapes: box, sphere, convex hull; bad input gives an empty shape") {
  const Shape box = Shape::Box({0.5f, 1.0f, 2.0f});
  REQUIRE(box.IsValid());
  CHECK(Near(box.Volume(), 8.0f, 1e-3f));
  CHECK(Near(box.LocalBounds().max.z, 2.0f, 1e-4f));

  const Shape sphere = Shape::Sphere(1.0f);
  CHECK(Near(sphere.Volume(), 4.0f / 3.0f * kPi, 1e-3f));

  // A flat square tile 1 x 1 x 0.09 (Snapfold's), its corners at +-0.045.
  std::vector<Vec3> corners;
  for (float z : {-0.045f, 0.045f}) {
    corners.insert(corners.end(), {{-0.5f, -0.5f, z}, {0.5f, -0.5f, z}, {0.5f, 0.5f, z}, {-0.5f, 0.5f, z}});
  }
  const Shape tile = Shape::ConvexHull(corners, 0.02f);
  REQUIRE(tile.IsValid());
  CHECK(Near(tile.Volume(), 0.09f, 2e-3f));
  CHECK(Near(glm::length(tile.CenterOfMass()), 0.0f, 1e-4f));

  const Shape copy = tile;  // copies share the shape
  CHECK(copy == tile);
  CHECK_FALSE(copy == box);

  CHECK_FALSE(Shape().IsValid());
  CHECK_FALSE(Shape::Box({1.0f, 0.0f, 1.0f}).IsValid());
  CHECK_FALSE(Shape::Sphere(-1.0f).IsValid());
  CHECK_FALSE(Shape::ConvexHull(std::vector<Vec3>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}).IsValid());
}

TEST_CASE("a box falls onto a floor, rests on it and falls asleep") {
  auto world = World::Create(TestSettings());
  AddFloor(*world);
  const BodyId box = world->CreateBody(
      {.shape = Shape::Box(Vec3(0.5f)), .position = {0.0f, 0.0f, 3.0f}, .layer = kMoving, .mass = 2.0f});
  REQUIRE(world->Contains(box));
  CHECK(world->BodyCount() == 2);
  CHECK(Near(world->Mass(box), 2.0f));

  world->Step(kStep);
  CHECK(world->Position(box).z < 3.0f);
  CHECK(world->LinearVelocity(box).z < 0.0f);

  Run(*world, 3.0f);
  CHECK(Near(world->Position(box).z, 0.5f, 0.03f));  // resting on the floor (within the penetration slop)
  CHECK_FALSE(world->IsAwake(box));
  CHECK(world->AwakeBodyCount() == 0);

  world->SetAwake(box, true);
  CHECK(world->IsAwake(box));
}

TEST_CASE("default mass = volume x 1000; static bodies have none") {
  auto world = World::Create(TestSettings());
  const BodyId floor = AddFloor(*world);
  const BodyId box = world->CreateBody({.shape = Shape::Box(Vec3(0.5f)), .layer = kMoving});
  CHECK(Near(world->Mass(box), 1000.0f, 0.5f));
  CHECK(world->Mass(floor) == 0.0f);
  CHECK(world->GetMotionType(floor) == MotionType::Static);
  CHECK(world->GetMotionType(box) == MotionType::Dynamic);
}

TEST_CASE("layers: a body in a layer that collides with nothing falls through the floor") {
  auto world = World::Create(TestSettings());
  AddFloor(*world);
  const BodyId ghost =
      world->CreateBody({.shape = Shape::Box(Vec3(0.5f)), .position = {0.0f, 0.0f, 1.0f}, .layer = kGhost});
  const BodyId solid =
      world->CreateBody({.shape = Shape::Box(Vec3(0.5f)), .position = {3.0f, 0.0f, 1.0f}, .layer = kMoving});
  Run(*world, 1.5f);
  CHECK(world->Position(ghost).z < -2.0f);
  CHECK(world->Position(solid).z > 0.4f);

  // Moving it to a colliding layer
  world->SetLayer(ghost, kMoving);
  CHECK(world->GetLayer(ghost) == kMoving);

  CollisionLayers layers(3);
  CHECK(layers.Collides(0, 2));
  layers.SetCollidesWithNothing(2);
  CHECK_FALSE(layers.Collides(0, 2));
  CHECK_FALSE(layers.Collides(2, 2));
  CHECK(layers.Collides(0, 1));
  layers.SetCollides(1, 0, false);
  CHECK_FALSE(layers.Collides(0, 1));  // both ways
}

TEST_CASE("kinematic bodies reach their MoveKinematic target in one step and push dynamic ones") {
  WorldSettings settings = TestSettings();
  settings.gravity = Vec3(0.0f);
  auto world = World::Create(settings);
  const BodyId pusher = world->CreateBody({.shape = Shape::Box(Vec3(0.5f)),
                                           .position = {0.0f, 0.0f, 0.0f},
                                           .motion = MotionType::Kinematic,
                                           .layer = kMoving});
  const BodyId crate =
      world->CreateBody({.shape = Shape::Box(Vec3(0.5f)), .position = {1.5f, 0.0f, 0.0f}, .layer = kMoving});

  const Quat turned = Turn(0.3f, kUp);
  world->MoveKinematic(pusher, {0.1f, 0.0f, 0.0f}, turned, kStep);
  world->Step(kStep);
  CHECK(Near(world->Position(pusher).x, 0.1f, 1e-4f));
  CHECK(Near(glm::dot(world->Rotation(pusher), turned), 1.0f, 1e-4f));

  world->MoveKinematic(pusher, {0.1f, 0.0f, 0.0f}, Quat(1, 0, 0, 0), kStep);
  world->Step(kStep);
  for (int i = 0; i < 60; ++i) {
    const float x = 0.1f + 0.05f * static_cast<float>(i + 1);
    world->MoveKinematic(pusher, {x, 0.0f, 0.0f}, Quat(1, 0, 0, 0), kStep);
    world->Step(kStep);
  }
  CHECK(world->Position(crate).x > 2.0f);  // pushed along
  CHECK(Near(world->Position(pusher).x, 3.1f, 1e-3f));
}

TEST_CASE("forces act for one step, impulses at once") {
  WorldSettings settings = TestSettings();
  settings.gravity = Vec3(0.0f);
  auto world = World::Create(settings);
  const BodyId body = world->CreateBody(
      {.shape = Shape::Sphere(0.5f), .layer = kMoving, .mass = 2.0f, .linear_damping = 0.0f, .angular_damping = 0.0f});
  world->AddForce(body, {4.0f, 0.0f, 0.0f});
  world->Step(kStep);
  CHECK(Near(world->LinearVelocity(body).x, 2.0f * kStep, 1e-5f));  // F / m x dt
  world->Step(kStep);
  CHECK(Near(world->LinearVelocity(body).x, 2.0f * kStep, 1e-5f));  // gone after one step

  world->AddImpulse(body, {0.0f, 2.0f, 0.0f});
  CHECK(Near(world->LinearVelocity(body).y, 1.0f, 1e-5f));

  // Off-centre: the body starts turning
  world->AddImpulseAt(body, {0.0f, 0.0f, 1.0f}, world->CenterOfMass(body) + Vec3(0.5f, 0.0f, 0.0f));
  CHECK(std::fabs(world->AngularVelocity(body).y) > 0.1f);
  CHECK(glm::length(world->PointVelocity(body, world->CenterOfMass(body))) > 0.0f);
}

TEST_CASE("motion types: kinematic to dynamic falls; a static body needs allow_motion_change") {
  auto world = World::Create(TestSettings());
  const BodyId held = world->CreateBody({.shape = Shape::Box(Vec3(0.5f)),
                                         .position = {0.0f, 0.0f, 5.0f},
                                         .motion = MotionType::Kinematic,
                                         .layer = kMoving});
  Run(*world, 0.5f);
  CHECK(Near(world->Position(held).z, 5.0f));  // kinematic: gravity doesn't move it
  world->SetMotionType(held, MotionType::Dynamic);
  CHECK(world->GetMotionType(held) == MotionType::Dynamic);
  CHECK(world->IsAwake(held));
  Run(*world, 0.5f);
  CHECK(world->Position(held).z < 4.0f);

  const BodyId fixed = world->CreateBody({.shape = Shape::Box(Vec3(0.5f)),
                                          .position = {3.0f, 0.0f, 5.0f},
                                          .motion = MotionType::Static,
                                          .layer = kStatic});
  world->SetMotionType(fixed, MotionType::Dynamic);  // refused (logged)
  CHECK(world->GetMotionType(fixed) == MotionType::Static);

  const BodyId movable = world->CreateBody({.shape = Shape::Box(Vec3(0.5f)),
                                            .position = {6.0f, 0.0f, 5.0f},
                                            .motion = MotionType::Static,
                                            .layer = kMoving,
                                            .allow_motion_change = true});
  world->SetMotionType(movable, MotionType::Dynamic);
  CHECK(world->GetMotionType(movable) == MotionType::Dynamic);
  Run(*world, 0.5f);
  CHECK(world->Position(movable).z < 4.0f);
}

TEST_CASE("hinge angle: 0 when the normals agree, positive = body b turned counter-clockwise about the axis") {
  WorldSettings settings = TestSettings();
  settings.gravity = Vec3(0.0f);
  auto world = World::Create(settings);
  // Two flat tiles side by side along +Y, sharing the edge y = 0.5 (axis along +X).
  const Shape tile = Shape::Box({0.5f, 0.5f, 0.045f}, 0.02f);
  const BodyId a = world->CreateBody({.shape = tile, .position = {0.0f, 0.0f, 0.0f}, .layer = kMoving});
  const BodyId b = world->CreateBody({.shape = tile, .position = {0.0f, 1.0f, 0.0f}, .layer = kMoving});
  const JointId hinge = world->AddHinge({.body_a = a,
                                         .body_b = b,
                                         .point = {0.0f, 0.5f, 0.0f},
                                         .axis = kRight,
                                         .normal_a = kForward,  // a's outward direction across the edge
                                         .normal_b = kForward});
  REQUIRE(world->Contains(hinge));
  CHECK(world->JointCount() == 1);
  CHECK(Near(world->GetHinge(hinge).angle, 0.0f, 1e-4f));
  CHECK(Near(glm::dot(world->GetHinge(hinge).axis, kRight), 1.0f, 1e-4f));
  CHECK(Near(world->GetHinge(hinge).point.y, 0.5f, 1e-4f));

  // Fold b up by +90 degrees about +X around the edge: its centre goes from (0, 1, 0) to (0, 0.5, 0.5).
  world->SetPose(b, {0.0f, 0.5f, 0.5f}, Turn(kHalfPi, kRight));
  CHECK(Near(world->GetHinge(hinge).angle, kHalfPi, 1e-3f));
  world->SetPose(b, {0.0f, 0.5f, -0.5f}, Turn(-kHalfPi, kRight));
  CHECK(Near(world->GetHinge(hinge).angle, -kHalfPi, 1e-3f));

  const auto [first, second] = world->JointBodies(hinge);
  CHECK(first == a);
  CHECK(second == b);
}

TEST_CASE("hinge: a hanging body's weight shows in the force readback; limits stop it; friction holds it") {
  auto world = World::Create(TestSettings());
  const BodyId anchor = world->CreateBody({.shape = Shape::Box(Vec3(0.1f)),
                                           .position = {0.0f, 0.0f, 5.0f},
                                           .motion = MotionType::Static,
                                           .layer = kStatic});
  // A lever along +Y from the hinge at the anchor, horizontal, 2 kg.
  const Shape lever = Shape::Box({0.05f, 0.5f, 0.05f}, 0.01f);
  auto make_lever = [&](float x) {
    return world->CreateBody(
        {.shape = lever, .position = {x, 0.5f, 5.0f}, .layer = kMoving, .mass = 2.0f, .allow_sleeping = false});
  };
  auto hinge_at = [&](BodyId body, float x, float friction, float limit) {
    return world->AddHinge({.body_a = anchor,
                            .body_b = body,
                            .point = {x, 0.0f, 5.0f},
                            .axis = kRight,
                            .normal_a = kForward,
                            .normal_b = kForward,
                            .min_angle = -limit,
                            .max_angle = limit,
                            .friction_torque = friction});
  };
  const BodyId free_lever = make_lever(0.0f);
  const BodyId limited_lever = make_lever(1.0f);
  const BodyId stiff_lever = make_lever(2.0f);
  const JointId free_hinge = hinge_at(free_lever, 0.0f, 0.0f, kPi);
  const JointId limited_hinge = hinge_at(limited_lever, 1.0f, 0.0f, 0.5f);
  // Gravity's torque on the lever: 2 kg x 9.81 x 0.5 m = 9.81 N m.
  const JointId stiff_hinge = hinge_at(stiff_lever, 2.0f, 15.0f, kPi);

  Run(*world, 4.0f);
  // Falls down (turning clockwise about +X: negative) and swings; hanging straight down = -90 degrees, damped
  // only by the bodies' damping, so somewhere in its swing.
  CHECK(world->GetHinge(free_hinge).angle < -0.3f);
  CHECK(Near(world->GetHinge(limited_hinge).angle, -0.5f, 0.05f));  // resting on its limit
  CHECK(world->GetHinge(limited_hinge).limit_torque > 1.0f);
  CHECK(Near(world->GetHinge(stiff_hinge).angle, 0.0f, 0.05f));  // friction holds it up

  // The friction hinge carries the lever's weight: 2 kg x 9.81 = 19.6 N up, plus the bending torque.
  const HingeState held = world->GetHinge(stiff_hinge);
  CHECK(Near(glm::length(held.force), 19.62f, 1.0f));
  CHECK(held.force.z > 0.0f);

  // Less friction than gravity's torque: it falls.
  world->SetHingeFriction(stiff_hinge, 2.0f);
  Run(*world, 1.0f);
  CHECK(world->GetHinge(stiff_hinge).angle < -0.3f);

  // Wider limits let the limited one fall further
  world->SetHingeLimits(limited_hinge, -1.2f, 1.2f);
  Run(*world, 2.0f);
  CHECK(world->GetHinge(limited_hinge).angle < -0.8f);
}

TEST_CASE("hinge: joined bodies don't collide with each other unless collide_connected") {
  WorldSettings settings = TestSettings();
  settings.gravity = Vec3(0.0f);
  auto world = World::Create(settings);
  const Shape box = Shape::Box(Vec3(0.5f));
  // b overlaps a by half; the hinge is on their shared top edge, so contacts would swing b about it.
  auto setup = [&](float y, bool collide) {
    const BodyId a =
        world->CreateBody({.shape = box, .position = {0.0f, y, 0.0f}, .motion = MotionType::Static, .layer = kMoving});
    const BodyId b = world->CreateBody({.shape = box, .position = {0.5f, y, 0.0f}, .layer = kMoving, .mass = 1.0f});
    const JointId joint = world->AddHinge({.body_a = a,
                                           .body_b = b,
                                           .point = {0.5f, y, 0.5f},
                                           .axis = kForward,
                                           .normal_a = kRight,
                                           .normal_b = kRight,
                                           .collide_connected = collide});
    return std::pair{b, joint};
  };
  const auto [quiet, quiet_joint] = setup(0.0f, false);
  const auto [pushed, pushed_joint] = setup(5.0f, true);
  Run(*world, 0.5f);
  CHECK(Near(world->Position(quiet).x, 0.5f, 1e-3f));  // nothing pushes it
  CHECK(Near(glm::dot(world->Rotation(quiet), Quat(1, 0, 0, 0)), 1.0f, 1e-4f));
  CHECK(std::fabs(world->GetHinge(pushed_joint).angle) > 0.05f);  // contacts turned it about the hinge

  // Without the joint, the pair collides again.
  world->RemoveJoint(quiet_joint);
  CHECK_FALSE(world->Contains(quiet_joint));
  Run(*world, 0.5f);
  CHECK(world->Position(quiet).x > 0.6f);
}

TEST_CASE("ids: destroyed bodies and removed joints stay invalid; DestroyBody removes its joints") {
  auto world = World::Create(TestSettings());
  const Shape box = Shape::Box(Vec3(0.5f));
  const BodyId a = world->CreateBody({.shape = box, .layer = kMoving});
  const BodyId b = world->CreateBody({.shape = box, .position = {1.0f, 0.0f, 0.0f}, .layer = kMoving});
  const JointId joint = world->AddHinge({.body_a = a,
                                         .body_b = b,
                                         .point = {0.5f, 0.0f, 0.0f},
                                         .axis = kForward,
                                         .normal_a = kRight,
                                         .normal_b = kRight});
  world->SetUserData(b, 42);
  CHECK(world->UserData(b) == 42);

  world->DestroyBody(b);
  CHECK_FALSE(world->Contains(b));
  CHECK_FALSE(world->Contains(joint));
  CHECK(world->JointCount() == 0);
  CHECK(world->BodyCount() == 1);
  CHECK(world->UserData(b) == 0);
  CHECK(world->Position(b) == Vec3(0.0f));
  world->SetLinearVelocity(b, {1.0f, 0.0f, 0.0f});  // does nothing
  world->DestroyBody(b);                            // twice: nothing

  // A new body may take the old slot, never the old id; a new joint likewise.
  const BodyId c = world->CreateBody({.shape = box, .position = {1.0f, 0.0f, 0.0f}, .layer = kMoving});
  CHECK(c != b);
  const JointId again = world->AddHinge({.body_a = a,
                                         .body_b = c,
                                         .point = {0.5f, 0.0f, 0.0f},
                                         .axis = kForward,
                                         .normal_a = kRight,
                                         .normal_b = kRight});
  CHECK(again.index == joint.index);
  CHECK(again != joint);
  CHECK(world->Contains(again));
  CHECK_FALSE(world->Contains(joint));
  CHECK(world->GetHinge(joint).axis == Vec3(0.0f));

  // Bad joints are refused (logged)
  CHECK_FALSE(world->AddHinge({.body_a = a, .body_b = a}).IsValid());
  CHECK_FALSE(world->AddHinge({.body_a = a, .body_b = b}).IsValid());
  CHECK_FALSE(world->CreateBody({.layer = kMoving}).IsValid());          // no shape
  CHECK_FALSE(world->CreateBody({.shape = box, .layer = 7}).IsValid());  // no such layer
}

TEST_CASE("ray casts: the nearest hit, its normal; layer mask, ignored body and accept filter") {
  auto world = World::Create(TestSettings());
  const BodyId floor = AddFloor(*world);
  const BodyId box = world->CreateBody({.shape = Shape::Box(Vec3(0.5f)),
                                        .position = {0.0f, 0.0f, 0.5f},
                                        .motion = MotionType::Static,
                                        .layer = kMoving});
  const BodyId ghost = world->CreateBody({.shape = Shape::Box(Vec3(0.5f)),
                                          .position = {0.0f, 0.0f, 3.0f},
                                          .motion = MotionType::Kinematic,
                                          .layer = kGhost});

  const Vec3 origin{0.0f, 0.0f, 10.0f};
  const std::optional<RayHit> hit = world->CastRay(origin, kDown, 100.0f);
  REQUIRE(hit);
  CHECK(hit->body == ghost);  // queries find bodies in a layer that collides with nothing
  CHECK(Near(hit->distance, 6.5f, 1e-3f));

  const auto solid = world->CastRay(origin, kDown * 3.0f, 100.0f, {.layers = LayerBit(kStatic) | LayerBit(kMoving)});
  REQUIRE(solid);
  CHECK(solid->body == box);
  CHECK(Near(solid->distance, 9.0f, 1e-3f));
  CHECK(Near(solid->point.z, 1.0f, 1e-3f));
  CHECK(Near(solid->normal.z, 1.0f, 1e-3f));

  const auto past =
      world->CastRay(origin, kDown, 100.0f, {.layers = LayerBit(kStatic) | LayerBit(kMoving), .ignore = box});
  REQUIRE(past);
  CHECK(past->body == floor);
  CHECK(Near(past->point.z, 0.0f, 1e-3f));

  const auto accepted = world->CastRay(origin, kDown, 100.0f, {.accept = [&](BodyId body) { return body == floor; }});
  REQUIRE(accepted);
  CHECK(accepted->body == floor);

  CHECK_FALSE(world->CastRay(origin, kDown, 5.0f, {.layers = LayerBit(kMoving)}));  // too short
  CHECK_FALSE(world->CastRay(origin, kUp, 100.0f));                                 // nothing above
  CHECK_FALSE(world->CastRay(origin, Vec3(0.0f), 100.0f));                          // no direction
}

TEST_CASE("worker threads give the same simulation as one thread") {
  auto simulate = [](int threads) {
    WorldSettings settings = TestSettings();
    settings.worker_threads = threads;
    auto world = World::Create(settings);
    AddFloor(*world);
    std::vector<BodyId> boxes;
    const Shape box = Shape::Box(Vec3(0.25f), 0.02f);
    for (int i = 0; i < 40; ++i) {
      const float fi = static_cast<float>(i);
      boxes.push_back(world->CreateBody({.shape = box,
                                         .position = {0.3f * std::sin(fi), 0.3f * std::cos(fi), 0.6f + 0.55f * fi},
                                         .rotation = Turn(0.1f * fi, kUp),
                                         .layer = kMoving}));
    }
    Run(*world, 2.0f);
    std::vector<Vec3> positions;
    for (BodyId id : boxes) {
      positions.push_back(world->Position(id));
    }
    return positions;
  };
  const std::vector<Vec3> one = simulate(0);
  const std::vector<Vec3> three = simulate(3);
  REQUIRE(one.size() == three.size());
  bool same = true;
  for (size_t i = 0; i < one.size(); ++i) {
    same = same && one[i] == three[i];
  }
  CHECK(same);
}

TEST_CASE("an open box of 5 magnet tiles and 8 hinges stands (Snapfold's scale and solver)") {
  // Tile edge 1, thickness 0.09, 1 kg, gravity 130: the benchmark's box (benchmarks/physics_benchmarks.cpp).
  constexpr float half = 0.045f;
  constexpr float g = 130.0f;
  WorldSettings settings = TestSettings();
  settings.gravity = {0.0f, 0.0f, -g};
  settings.solver = {
      .velocity_steps = 12, .position_steps = 4, .penetration_slop = 0.004f, .speculative_distance = 0.012f};
  auto world = World::Create(settings);
  AddFloor(*world);
  const Shape tile = Shape::Box({0.5f, 0.5f, half}, 0.02f);
  auto add = [&](Vec3 position, Quat rotation) {
    return world->CreateBody({.shape = tile,
                              .position = position,
                              .rotation = rotation,
                              .layer = kMoving,
                              .mass = 1.0f,
                              .friction = 0.7f,
                              .angular_damping = 0.15f});
  };
  const float wall_z = half + 0.5f;
  const BodyId floor = add({0.0f, 0.0f, half}, Quat(1, 0, 0, 0));
  const BodyId east = add({0.5f, 0.0f, wall_z}, Turn(kHalfPi, kForward));
  const BodyId north = add({0.0f, 0.5f, wall_z}, Turn(-kHalfPi, kRight));
  const BodyId west = add({-0.5f, 0.0f, wall_z}, Turn(-kHalfPi, kForward));
  const BodyId south = add({0.0f, -0.5f, wall_z}, Turn(kHalfPi, kRight));
  std::vector<JointId> hinges;
  auto hinge = [&](BodyId a, BodyId b, Vec3 point, Vec3 axis, Vec3 normal_a, Vec3 normal_b) {
    hinges.push_back(world->AddHinge({.body_a = a,
                                      .body_b = b,
                                      .point = point,
                                      .axis = axis,
                                      .normal_a = normal_a,
                                      .normal_b = normal_b,
                                      .friction_torque = 0.06f * g}));
  };
  hinge(floor, east, {0.5f, 0.0f, half}, kForward, kRight, kUp);
  hinge(floor, north, {0.0f, 0.5f, half}, kRight, kForward, kUp);
  hinge(floor, west, {-0.5f, 0.0f, half}, kForward, kLeft, kUp);
  hinge(floor, south, {0.0f, -0.5f, half}, kRight, kBack, kUp);
  hinge(east, north, {0.5f, 0.5f, wall_z}, kUp, kForward, kLeft);
  hinge(north, west, {-0.5f, 0.5f, wall_z}, kUp, kLeft, kBack);
  hinge(west, south, {-0.5f, -0.5f, wall_z}, kUp, kBack, kRight);
  hinge(south, east, {0.5f, -0.5f, wall_z}, kUp, kRight, kForward);
  // Folded up: the walls are at +90 degrees from the floor, the corners at 90 too.
  for (JointId id : hinges) {
    CHECK(Near(std::fabs(world->GetHinge(id).angle), kHalfPi, 1e-3f));
  }

  Run(*world, 2.0f, 1.0f / 120.0f);
  CHECK(Near(world->Position(floor).z, half, 0.02f));
  for (BodyId wall : {east, north, west, south}) {
    CHECK(Near(world->Position(wall).z, wall_z, 0.02f));
  }
  for (JointId id : hinges) {
    CHECK(Near(std::fabs(world->GetHinge(id).angle), kHalfPi, 0.02f));
  }
  // The walls' weight goes down the floor hinges (the corners only pass forces between walls: a closed loop, so
  // one hinge alone can carry more or less than its wall): 4 x 1 kg x 130 in all.
  float carried = 0.0f;
  for (int i = 0; i < 4; ++i) {
    carried += world->GetHinge(hinges[i]).force.z;
  }
  CHECK(Near(carried, 4.0f * g, 0.05f * 4.0f * g));
  CHECK_FALSE(world->IsAwake(floor));  // and it all fell asleep
}

TEST_CASE("physics view: shapes as see-through triangles by state, joints as lines, inside the bodies' reach") {
  WorldSettings settings = TestSettings();
  settings.gravity = Vec3(0.0f);
  auto world = World::Create(settings);
  const Shape tile = Shape::Box({0.5f, 0.5f, 0.045f}, 0.02f);
  const BodyId a = world->CreateBody({.shape = tile, .position = {0.0f, 0.0f, 1.0f}, .layer = kMoving});
  const BodyId b = world->CreateBody({.shape = tile, .position = {0.0f, 1.0f, 1.0f}, .layer = kMoving});
  REQUIRE(world->AddHinge({.body_a = a, .body_b = b, .point = {0.0f, 0.5f, 1.0f}, .axis = kRight}).IsValid());

  debug::DebugDraw draw;
  world->DebugDraw(draw, {.shape_alpha = 0.4f, .joint_size = 0.3f});
  const auto& lists = draw.Internal().Lists();
  if constexpr (!debug::kDebugDrawEnabled) {
    CHECK(lists.vertices.empty());  // Jolt built without its debug renderer: nothing
    return;
  }
  REQUIRE(lists.vertices.size() >= 2 * 12 * 3);  // two boxes' faces (rounded corners: more)
  CHECK_FALSE(lists.lines.empty());              // the hinge's axes and markers
  // Everything within the two tiles, grown by the hinge markers' size.
  const AABB reach = AABB{{-0.5f, -0.5f, 0.955f}, {0.5f, 1.5f, 1.045f}}.Grown(0.31f);
  for (const debug::DebugVertex& corner : lists.vertices) CHECK(reach.Contains(corner.position));
  for (const debug::DebugLine& line : lists.lines) {
    CHECK(reach.Contains(line.a));
    CHECK(reach.Contains(line.b));
  }
  // Awake dynamic bodies are yellow (State colours), see-through at the alpha asked for.
  const Color32 color = Color32::FromPacked(lists.vertices[0].color);
  CHECK(color.r > 100);
  CHECK(color.g > 100);
  CHECK(color.b < 40);
  CHECK(color.a == Color32(Color(1.0f, 1.0f, 1.0f, 0.4f)).a);

  // Asleep: red. Shapes only (no joints): no lines.
  world->SetAwake(a, false);
  world->SetAwake(b, false);
  draw.Clear();
  world->DebugDraw(draw, {.joints = false});
  const Color32 asleep = Color32::FromPacked(draw.Internal().Lists().vertices.at(0).color);
  CHECK(asleep.r > 100);
  CHECK(asleep.g < 40);
  CHECK(draw.Internal().Lists().lines.empty());

  // Debug draw off: nothing.
  draw.SetEnabled(false);
  world->DebugDraw(draw);
  CHECK(draw.Stats().triangles == 0);
}

TEST_SUITE_END();
