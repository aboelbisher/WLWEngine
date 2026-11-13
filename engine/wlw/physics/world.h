#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "wlw/core/math.h"
#include "wlw/debug/debug_draw.h"
#include "wlw/physics/shape.h"

// wlw::physics: 3D rigid-body physics on Jolt Physics (ROADMAP 18.1, D10), hidden behind these headers. Its own
// target, wlw::physics, built only when WLW_BUILD_PHYSICS is on (a game that uses it turns it on and links it).
//
// The shape of the API is the physics "server" of the big engines: Jolt's BodyInterface, Box2D v3's ids, Godot's
// PhysicsServer3D with RIDs. Bodies and joints are plain ids into a World the game owns; the game steps it from its
// fixed update and copies poses to its nodes (body nodes come with scenes as data). Units are the engine's (meters,
// kilograms, seconds, radians; Z up) unless a game picks its own scale and sets gravity and the solver to match.
//
//   auto world = physics::World::Create({.layers = layers, .gravity = {0, 0, -9.81f}});
//   physics::BodyId box = world->CreateBody({.shape = physics::Shape::Box(Vec3(0.5f)), .position = {0, 0, 3}});
//   void OnFixedUpdate(float dt) override { world->Step(dt); }
//   node->SetPosition(world->Position(box)); node->SetRotation(world->Rotation(box));
//
// Threads: a World is used from one thread (the game thread). Step may run Jolt's jobs on worker threads
// (WorldSettings::worker_threads) and returns when they're done. Shapes may be made on any thread.
namespace wlw::physics {

// A body in a World. Ids of destroyed bodies are never valid again (Jolt's sequence numbers): reading one returns
// zeros, changing one does nothing.
struct BodyId {
  static constexpr uint32_t kInvalid = 0xffffffffu;
  uint32_t value = kInvalid;

  constexpr bool IsValid() const { return value != kInvalid; }
  friend constexpr bool operator==(BodyId a, BodyId b) = default;
};

// A joint in a World (index + generation: a removed joint's id stays invalid).
struct JointId {
  static constexpr uint32_t kInvalid = 0xffffffffu;
  uint32_t index = kInvalid;
  uint32_t generation = 0;

  constexpr bool IsValid() const { return index != kInvalid; }
  friend constexpr bool operator==(JointId a, JointId b) = default;
};

enum class MotionType : uint8_t {
  Static,     // never moves (floors, walls); costs nothing while nothing touches it
  Kinematic,  // moved by the game (MoveKinematic / velocities), pushes dynamic bodies, nothing pushes it
  Dynamic,    // moved by the simulation: gravity, forces, contacts, joints
};

// Object layers and which pairs of them collide: Unity's layer collision matrix, Jolt's ObjectLayerPairFilterTable,
// Godot's layer / mask bits. Every pair collides until told otherwise. Each layer is its own broad-phase tree, so
// keep them few (statics in their own layer: Jolt's advice). Queries pick layers with a mask (LayerBit).
using Layer = uint8_t;
inline constexpr int kMaxLayers = 16;
constexpr uint32_t LayerBit(Layer layer) { return 1u << layer; }
inline constexpr uint32_t kAllLayers = 0xffffu;

class CollisionLayers {
 public:
  CollisionLayers() = default;          // one layer (0) that collides with itself
  explicit CollisionLayers(int count);  // 1..kMaxLayers layers, 0..count-1, every pair colliding

  int Count() const { return count_; }
  void SetCollides(Layer a, Layer b, bool collides);  // both ways; a == b = bodies of one layer with each other
  bool Collides(Layer a, Layer b) const;
  // The layer collides with nothing (bodies in it are still found by queries that ask for it).
  void SetCollidesWithNothing(Layer layer);

 private:
  int count_ = 1;
  std::array<uint16_t, kMaxLayers> masks_{1};  // bit b of masks_[a]: a collides with b
};

// The solver (Jolt's PhysicsSettings; these defaults are Jolt's, tuned for meters).
struct SolverSettings {
  int velocity_steps = 10;             // iterations per step for velocities (contacts, joints)
  int position_steps = 2;              // iterations per step pushing bodies apart / joints together
  float baumgarte = 0.2f;              // 0..1: how much of a position error is fixed per step
  float penetration_slop = 0.02f;      // m: overlap allowed before contacts push (keeps resting contacts still)
  float speculative_distance = 0.02f;  // m: contacts made this far before touching (smooth landings)
  bool allow_sleeping = true;
  float time_before_sleep = 0.5f;  // s a body must stay slow before it sleeps
  float sleep_velocity = 0.03f;    // m/s: "slow" = every point of the body moves slower than this
};

struct WorldSettings {
  CollisionLayers layers;
  Vec3 gravity{0.0f, 0.0f, -9.81f};
  SolverSettings solver;
  int max_bodies = 4096;
  int max_body_pairs = 16384;        // broad-phase pairs per step (more are dropped: logged)
  int max_contacts = 8192;           // contact constraints per step (more are dropped: logged)
  int temp_memory_bytes = 16 << 20;  // Jolt's per-step scratch memory
  // 0 = Step runs everything on the calling thread (no threads made; deterministic). n > 0 = a pool of n worker
  // threads (Jolt's JobSystemThreadPool) helps; the calling thread works too. Measure: small scenes on phones are
  // often faster single-threaded.
  int worker_threads = 0;
};

struct BodySettings {
  Shape shape;  // required
  Vec3 position{0.0f};
  Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  MotionType motion = MotionType::Dynamic;
  Layer layer = 0;
  float mass = 0.0f;              // kg; 0 = the shape's volume x 1000 kg/m^3 (inertia always from the shape)
  float friction = 0.2f;          // two bodies' friction combine as sqrt(a x b) (Jolt's default)
  float restitution = 0.0f;       // bounciness 0..1 (the larger of the two counts)
  float linear_damping = 0.05f;   // 1/s
  float angular_damping = 0.05f;  // 1/s
  float gravity_factor = 1.0f;
  bool continuous = false;           // continuous collision (Jolt's LinearCast): fast thin bodies don't tunnel
  bool allow_motion_change = false;  // a Static body may become Kinematic / Dynamic later (others always can)
  bool allow_sleeping = true;
  bool awake = true;       // a dynamic body starts awake; false = asleep until something touches or wakes it
  uint64_t user_data = 0;  // the game's own value (e.g. which tile this is), read back with UserData
};

// A hinge: two bodies turning about one shared axis (a door, a magnet tile's edge). Given in world space where the
// bodies are now: the bodies don't have to line up exactly (the solver pulls them together).
struct HingeSettings {
  BodyId body_a;
  BodyId body_b;             // at least one of the two is dynamic
  Vec3 point{0.0f};          // world: the point on the axis both bodies keep
  Vec3 axis = kRight;        // world, unit: the axis they turn about
  Vec3 normal_a = kForward;  // world, unit, perpendicular to axis: a direction fixed to body a ...
  Vec3 normal_b = kForward;  // ... and one fixed to body b; the angle is 0 when they point the same way
  float min_angle = -kPi;    // radians: limits of the angle, min in [-pi, 0], max in [0, pi]; +-pi = no limit
  float max_angle = kPi;
  float friction_torque = 0.0f;    // N m: the most torque friction applies against turning
  bool collide_connected = false;  // the two bodies still collide with each other (off, like Box2D / Godot)
};

// What a hinge did in the last step. The constraint forces are the impulses the solver needed over the last
// (collision) step divided by its time: what a joint's strength is checked against (breakable joints).
struct HingeState {
  float angle = 0.0f;         // radians: body b turned about the axis from where normal_b = normal_a, (-pi, pi]
  float speed = 0.0f;         // rad/s: how fast the angle changes now
  Vec3 point{0.0f};           // world: the hinge point (body a's)
  Vec3 axis{0.0f};            // world: the axis (body a's)
  Vec3 force{0.0f};           // N, world: what held the two bodies' points together
  float torque = 0.0f;        // N m: what kept the axes lined up (bending the hinge)
  float limit_torque = 0.0f;  // N m: what the limits pushed back with
};

struct RayHit {
  BodyId body;
  float distance = 0.0f;  // from the origin along the direction
  Vec3 point{0.0f};
  Vec3 normal{0.0f};  // the surface normal there (unit; a ray starting inside a body hits at distance 0)
};

// What World::DebugDraw shows: the physics view (Jolt's own debug drawing, like Jolt's Samples app).
struct PhysicsDrawSettings {
  enum class Colors : uint8_t {
    State,   // static grey, kinematic green, dynamic yellow while awake, red asleep (the sleep is visible)
    Body,    // a different colour per body
    Island,  // bodies that are solved together (touching or joined) share a colour
  };
  bool shapes = true;      // the bodies' collision shapes
  bool wireframe = false;  // their triangles' edges instead of see-through solids (hull faces show as triangles)
  Colors colors = Colors::State;
  float shape_alpha = 0.35f;     // solid shapes' opacity
  bool bounding_boxes = false;   // each body's world box
  bool centers_of_mass = false;  // axes at each body's centre of mass
  bool velocities = false;       // arrows: linear velocity, angular velocity
  bool joints = true;            // each joint's points (a marker per body), axis (body a red, b green), normal (white)
  bool joint_limits = false;     // hinge limits as a pie
  bool joint_frames = false;     // the joints' reference frames
  float joint_size = 0.5f;       // how long the joints' axis and normal lines are, and the limits' radius
  float width = 2.0f;            // lines, dp
  debug::DepthMode depth = debug::DepthMode::Tested;
};

// Which bodies a query may hit.
struct QueryFilter {
  uint32_t layers = kAllLayers;        // LayerBit(...) | ...
  BodyId ignore;                       // one body never hit (e.g. the one asking)
  std::function<bool(BodyId)> accept;  // optional: false skips that body (called per candidate, during the query)
};

class World {
 public:
  static std::unique_ptr<World> Create(const WorldSettings& settings = {});
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // ---- Simulation ----
  // Advances the world by dt seconds (a constant fixed step: call it from OnFixedUpdate). collision_steps > 1 splits
  // the step (more stable fast bodies; costs as much as that many steps).
  void Step(float dt, int collision_steps = 1);
  void SetGravity(Vec3 gravity);
  Vec3 Gravity() const;
  // After adding many bodies at once (a level loading): rebuilds the broad phase's trees for faster queries.
  void OptimizeBroadPhase();

  // ---- Bodies ----
  // Makes the body and adds it to the world. Invalid id when the world is full or the shape is empty (logged).
  BodyId CreateBody(const BodySettings& settings);
  // Removes the body and every joint on it.
  void DestroyBody(BodyId body);
  bool Contains(BodyId body) const;

  Vec3 Position(BodyId body) const;  // the body's origin (its shape's origin)
  Quat Rotation(BodyId body) const;
  Mat4 Transform(BodyId body) const;     // position x rotation (for a node or instance)
  Vec3 CenterOfMass(BodyId body) const;  // world
  // Moves the body there at once (teleport) and wakes it. For kinematic bodies that should push others while
  // they move, use MoveKinematic.
  void SetPose(BodyId body, Vec3 position, Quat rotation);
  // Kinematic bodies: sets the velocities that take the body to the target in dt (the coming step's time), so it
  // pushes dynamic bodies on the way. Call it every step while it moves.
  void MoveKinematic(BodyId body, Vec3 target_position, Quat target_rotation, float dt);

  Vec3 LinearVelocity(BodyId body) const;
  Vec3 AngularVelocity(BodyId body) const;  // rad/s about the world axes
  Vec3 PointVelocity(BodyId body, Vec3 world_point) const;
  void SetLinearVelocity(BodyId body, Vec3 velocity);
  void SetAngularVelocity(BodyId body, Vec3 velocity);

  // Forces and torques act during the next Step only (call them every step while they push); they wake the body.
  // Impulses change the velocity at once.
  void AddForce(BodyId body, Vec3 force);  // at the centre of mass
  void AddForceAt(BodyId body, Vec3 force, Vec3 world_point);
  void AddTorque(BodyId body, Vec3 torque);
  void AddImpulse(BodyId body, Vec3 impulse);
  void AddImpulseAt(BodyId body, Vec3 impulse, Vec3 world_point);

  float Mass(BodyId body) const;  // kg (also for a kinematic body: its mass if it became dynamic); 0 static
  MotionType GetMotionType(BodyId body) const;
  // Static <-> moving needs BodySettings::allow_motion_change. Becoming dynamic wakes the body.
  void SetMotionType(BodyId body, MotionType motion);
  Layer GetLayer(BodyId body) const;
  void SetLayer(BodyId body, Layer layer);
  void SetFriction(BodyId body, float friction);

  bool IsAwake(BodyId body) const;
  void SetAwake(BodyId body, bool awake);  // waking a sleeping body wakes what it rests on / is joined to as well
  uint64_t UserData(BodyId body) const;
  void SetUserData(BodyId body, uint64_t value);

  // ---- Joints ----
  // Adding or removing a joint wakes its bodies. Invalid id when a body is missing or both are static (logged).
  JointId AddHinge(const HingeSettings& settings);
  void RemoveJoint(JointId joint);
  bool Contains(JointId joint) const;
  HingeState GetHinge(JointId joint) const;
  void SetHingeFriction(JointId joint, float friction_torque);
  void SetHingeLimits(JointId joint, float min_angle, float max_angle);
  std::pair<BodyId, BodyId> JointBodies(JointId joint) const;

  // ---- Queries ----
  // The nearest hit along the ray, at most max_distance away; direction need not be unit length.
  std::optional<RayHit> CastRay(Vec3 origin, Vec3 direction, float max_distance, const QueryFilter& filter = {}) const;

  // ---- Debug ----
  // The physics view: the world as the solver sees it, into `draw` (normally App().DebugDraw()). Call it from
  // OnUpdate: it draws the state the last step left, once per frame. Does nothing in builds without debug drawing
  // (debug::kDebugDrawEnabled; Jolt is built without its debug renderer then).
  void DebugDraw(debug::DebugDraw& draw, const PhysicsDrawSettings& settings = {});

  // ---- Stats ----
  int BodyCount() const;
  int AwakeBodyCount() const;
  int JointCount() const;
  float LastStepMs() const;  // wall time of the last Step on the calling thread

  struct Impl;

 private:
  explicit World(const WorldSettings& settings);
  std::unique_ptr<Impl> impl_;
};

}  // namespace wlw::physics

template <>
struct std::hash<wlw::physics::BodyId> {
  size_t operator()(wlw::physics::BodyId id) const noexcept { return std::hash<uint32_t>{}(id.value); }
};
template <>
struct std::hash<wlw::physics::JointId> {
  size_t operator()(wlw::physics::JointId id) const noexcept {
    return std::hash<uint64_t>{}((uint64_t{id.generation} << 32) | id.index);
  }
};
