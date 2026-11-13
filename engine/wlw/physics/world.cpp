#include "wlw/physics/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

#include "wlw/core/assert.h"
#include "wlw/core/profile.h"
#include "wlw/physics/jolt_internal.h"

// clang-format off
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

namespace wlw::physics {

using detail::ToJolt;
using detail::ToWlw;

// ---- CollisionLayers ----

CollisionLayers::CollisionLayers(int count) {
  WLW_ASSERT(count >= 1 && count <= kMaxLayers, "CollisionLayers: %d layers (1..%d)", count, kMaxLayers);
  count_ = std::clamp(count, 1, kMaxLayers);
  const uint16_t all = static_cast<uint16_t>((1u << count_) - 1u);
  for (int i = 0; i < count_; ++i) {
    masks_[i] = all;
  }
}

void CollisionLayers::SetCollides(Layer a, Layer b, bool collides) {
  if (!WLW_VERIFY(a < count_ && b < count_, "CollisionLayers: layer %d / %d of %d", a, b, count_)) {
    return;
  }
  if (collides) {
    masks_[a] = static_cast<uint16_t>(masks_[a] | (1u << b));
    masks_[b] = static_cast<uint16_t>(masks_[b] | (1u << a));
  } else {
    masks_[a] = static_cast<uint16_t>(masks_[a] & ~(1u << b));
    masks_[b] = static_cast<uint16_t>(masks_[b] & ~(1u << a));
  }
}

bool CollisionLayers::Collides(Layer a, Layer b) const {
  return a < count_ && b < count_ && (masks_[a] & (1u << b)) != 0;
}

void CollisionLayers::SetCollidesWithNothing(Layer layer) {
  for (int other = 0; other < count_; ++other) {
    SetCollides(layer, static_cast<Layer>(other), false);
  }
}

// ---- World ----

namespace {

// Pairs of bodies that must not collide (the bodies of joints made without collide_connected), counted: two joints
// between one pair block it until both are gone. Jolt asks during the step, from its jobs (read only then).
// Every body carries this filter with its body index as the sub-group; each body's list is tiny (its joints).
class PairFilter final : public JPH::GroupFilter {
 public:
  explicit PairFilter(int max_bodies) : blocked_(static_cast<size_t>(max_bodies)) {}

  bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
    const auto& list = blocked_[a.GetSubGroupID()];
    const JPH::CollisionGroup::SubGroupID other = b.GetSubGroupID();
    for (const Entry& entry : list) {
      if (entry.other == other) {
        return false;
      }
    }
    return true;
  }

  void Block(uint32_t a, uint32_t b) {
    Add(a, b);
    Add(b, a);
  }
  void Unblock(uint32_t a, uint32_t b) {
    Remove(a, b);
    Remove(b, a);
  }

 private:
  struct Entry {
    uint32_t other;
    uint32_t count;
  };
  void Add(uint32_t a, uint32_t b) {
    for (Entry& entry : blocked_[a]) {
      if (entry.other == b) {
        ++entry.count;
        return;
      }
    }
    blocked_[a].push_back({b, 1});
  }
  void Remove(uint32_t a, uint32_t b) {
    auto& list = blocked_[a];
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i].other == b) {
        if (--list[i].count == 0) {
          list[i] = list.back();
          list.pop_back();
        }
        return;
      }
    }
  }
  std::vector<std::vector<Entry>> blocked_;
};

// Query filters: a layer mask (broad phase layer i = object layer i), one ignored body and the game's callback.
class MaskBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter {
 public:
  explicit MaskBroadPhaseFilter(uint32_t mask) : mask_(mask) {}
  bool ShouldCollide(JPH::BroadPhaseLayer layer) const override {
    return (mask_ & (1u << static_cast<JPH::BroadPhaseLayer::Type>(layer))) != 0;
  }

 private:
  uint32_t mask_;
};

class MaskObjectFilter final : public JPH::ObjectLayerFilter {
 public:
  explicit MaskObjectFilter(uint32_t mask) : mask_(mask) {}
  bool ShouldCollide(JPH::ObjectLayer layer) const override { return (mask_ & (1u << layer)) != 0; }

 private:
  uint32_t mask_;
};

class QueryBodyFilter final : public JPH::BodyFilter {
 public:
  explicit QueryBodyFilter(const QueryFilter& filter) : filter_(filter) {}
  bool ShouldCollide(const JPH::BodyID& id) const override {
    const BodyId body{id.GetIndexAndSequenceNumber()};
    return body != filter_.ignore && (!filter_.accept || filter_.accept(body));
  }

 private:
  const QueryFilter& filter_;
};

JPH::BodyID ToJolt(BodyId id) { return JPH::BodyID(id.value); }
BodyId ToWlw(const JPH::BodyID& id) { return BodyId{id.GetIndexAndSequenceNumber()}; }

JPH::EMotionType ToJolt(MotionType motion) {
  switch (motion) {
    case MotionType::Static: return JPH::EMotionType::Static;
    case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic: return JPH::EMotionType::Dynamic;
  }
  return JPH::EMotionType::Dynamic;
}

MotionType ToWlw(JPH::EMotionType motion) {
  switch (motion) {
    case JPH::EMotionType::Static: return MotionType::Static;
    case JPH::EMotionType::Kinematic: return MotionType::Kinematic;
    case JPH::EMotionType::Dynamic: return MotionType::Dynamic;
  }
  return MotionType::Dynamic;
}

Quat NormalizedOrIdentity(const Quat& q) {
  const float length = glm::length(q);
  return length > 1e-6f ? q / length : Quat(1.0f, 0.0f, 0.0f, 0.0f);
}

}  // namespace

struct World::Impl {
  struct Joint {
    JPH::Ref<JPH::HingeConstraint> hinge;  // null = a free slot
    BodyId a;
    BodyId b;
    bool blocks_pair = false;
    uint32_t generation = 0;
  };

  explicit Impl(const WorldSettings& settings);
  ~Impl();

  JPH::BodyInterface& Bodies() { return system.GetBodyInterfaceNoLock(); }
  const JPH::BodyInterface& Bodies() const { return system.GetBodyInterfaceNoLock(); }
  const JPH::Body* Find(BodyId id) const { return system.GetBodyLockInterfaceNoLock().TryGetBody(ToJolt(id)); }
  JPH::Body* Find(BodyId id) { return system.GetBodyLockInterfaceNoLock().TryGetBody(ToJolt(id)); }
  const Joint* FindJoint(JointId id) const {
    if (id.index >= joints.size()) {
      return nullptr;
    }
    const Joint& joint = joints[id.index];
    return joint.hinge != nullptr && joint.generation == id.generation ? &joint : nullptr;
  }
  void RemoveJointAt(uint32_t index);

  int layer_count = 1;
  // The system keeps references to these: they're made first and destroyed after it.
  std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> broad_phase_layers;
  std::unique_ptr<JPH::ObjectLayerPairFilterTable> layer_pairs;
  std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> object_vs_broad_phase;
  std::unique_ptr<JPH::TempAllocator> temp;
  std::unique_ptr<JPH::JobSystem> jobs;
  JPH::Ref<PairFilter> pairs;
  JPH::PhysicsSystem system;

  std::vector<Joint> joints;
  std::vector<uint32_t> free_joints;
  int joint_count = 0;
  float last_substep_dt = 0.0f;
  float last_step_ms = 0.0f;
  JPH::EPhysicsUpdateError reported_errors = JPH::EPhysicsUpdateError::None;
};

World::Impl::Impl(const WorldSettings& settings) {
  detail::EnsureJolt();
  const CollisionLayers& layers = settings.layers;
  layer_count = layers.Count();
  const auto count = static_cast<JPH::uint>(layer_count);

  // One broad-phase layer (tree) per object layer.
  broad_phase_layers = std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(count, count);
  layer_pairs = std::make_unique<JPH::ObjectLayerPairFilterTable>(count);
  for (JPH::uint a = 0; a < count; ++a) {
    broad_phase_layers->MapObjectToBroadPhaseLayer(static_cast<JPH::ObjectLayer>(a),
                                                   JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(a)));
    for (JPH::uint b = a; b < count; ++b) {
      if (layers.Collides(static_cast<Layer>(a), static_cast<Layer>(b))) {
        layer_pairs->EnableCollision(static_cast<JPH::ObjectLayer>(a), static_cast<JPH::ObjectLayer>(b));
      }
    }
  }
  object_vs_broad_phase =
      std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(*broad_phase_layers, count, *layer_pairs, count);

#ifdef JPH_DISABLE_TEMP_ALLOCATOR
  temp = std::make_unique<JPH::TempAllocatorMalloc>();
#else
  temp =
      std::make_unique<JPH::TempAllocatorImpl>(static_cast<JPH::uint>(std::max(settings.temp_memory_bytes, 1 << 20)));
#endif
  if (settings.worker_threads > 0) {
    jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                      settings.worker_threads);
  } else {
    jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
  }

  const int max_bodies = std::clamp(settings.max_bodies, 1, 1 << 22);
  pairs = new PairFilter(max_bodies);
  system.Init(static_cast<JPH::uint>(max_bodies), 0, static_cast<JPH::uint>(std::max(settings.max_body_pairs, 16)),
              static_cast<JPH::uint>(std::max(settings.max_contacts, 16)), *broad_phase_layers, *object_vs_broad_phase,
              *layer_pairs);

  JPH::PhysicsSettings jolt = system.GetPhysicsSettings();
  const SolverSettings& solver = settings.solver;
  jolt.mNumVelocitySteps = static_cast<JPH::uint>(std::max(solver.velocity_steps, 1));
  jolt.mNumPositionSteps = static_cast<JPH::uint>(std::max(solver.position_steps, 0));
  jolt.mBaumgarte = solver.baumgarte;
  jolt.mPenetrationSlop = solver.penetration_slop;
  jolt.mSpeculativeContactDistance = solver.speculative_distance;
  jolt.mAllowSleeping = solver.allow_sleeping;
  jolt.mTimeBeforeSleep = solver.time_before_sleep;
  jolt.mPointVelocitySleepThreshold = solver.sleep_velocity;
  system.SetPhysicsSettings(jolt);
  system.SetGravity(ToJolt(settings.gravity));
}

World::Impl::~Impl() {
  // Joints first (they hold references to their bodies), then the bodies.
  for (uint32_t i = 0; i < joints.size(); ++i) {
    if (joints[i].hinge != nullptr) {
      system.RemoveConstraint(joints[i].hinge);
      joints[i].hinge = nullptr;
    }
  }
  JPH::BodyIDVector ids;
  system.GetBodies(ids);
  if (!ids.empty()) {
    JPH::BodyInterface& bodies = Bodies();
    bodies.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
    bodies.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
  }
}

void World::Impl::RemoveJointAt(uint32_t index) {
  Joint& joint = joints[index];
  system.RemoveConstraint(joint.hinge);
  joint.hinge = nullptr;
  if (joint.blocks_pair) {
    pairs->Unblock(ToJolt(joint.a).GetIndex(), ToJolt(joint.b).GetIndex());
  }
  // The bodies were held by the joint: let them fall / move again.
  for (BodyId body : {joint.a, joint.b}) {
    if (Bodies().IsAdded(ToJolt(body)) && Bodies().GetMotionType(ToJolt(body)) != JPH::EMotionType::Static) {
      Bodies().ActivateBody(ToJolt(body));
    }
  }
  ++joint.generation;
  free_joints.push_back(index);
  --joint_count;
}

std::unique_ptr<World> World::Create(const WorldSettings& settings) {
  return std::unique_ptr<World>(new World(settings));
}

World::World(const WorldSettings& settings) : impl_(std::make_unique<Impl>(settings)) {}
World::~World() = default;

// ---- Simulation ----

void World::Step(float dt, int collision_steps) {
  WLW_PROFILE_SCOPE("physics::World::Step");
  if (dt <= 0.0f) {
    return;
  }
  collision_steps = std::max(collision_steps, 1);
  const auto start = std::chrono::steady_clock::now();
  const JPH::EPhysicsUpdateError errors =
      impl_->system.Update(dt, collision_steps, impl_->temp.get(), impl_->jobs.get());
  impl_->last_step_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
  impl_->last_substep_dt = dt / static_cast<float>(collision_steps);
  // Each kind of overflow once per world (they repeat every step while the scene is too big).
  const auto fresh = static_cast<uint32_t>(errors) & ~static_cast<uint32_t>(impl_->reported_errors);
  if (fresh != 0) {
    impl_->reported_errors =
        static_cast<JPH::EPhysicsUpdateError>(static_cast<uint32_t>(impl_->reported_errors) | fresh);
    WLW_LOG(LogPhysics, Warning, "physics: a step ran out of room (%s%s%s): raise WorldSettings' limits",
            (fresh & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ManifoldCacheFull)) ? "contact cache " : "",
            (fresh & static_cast<uint32_t>(JPH::EPhysicsUpdateError::BodyPairCacheFull)) ? "max_body_pairs " : "",
            (fresh & static_cast<uint32_t>(JPH::EPhysicsUpdateError::ContactConstraintsFull)) ? "max_contacts" : "");
  }
}

void World::SetGravity(Vec3 gravity) { impl_->system.SetGravity(ToJolt(gravity)); }
Vec3 World::Gravity() const { return ToWlw(impl_->system.GetGravity()); }
void World::OptimizeBroadPhase() { impl_->system.OptimizeBroadPhase(); }

// ---- Bodies ----

BodyId World::CreateBody(const BodySettings& settings) {
  const JoltShape* shape = Shape::InternalAccess::Get(settings.shape);
  if (!WLW_VERIFY(shape != nullptr, "physics: CreateBody without a shape")) {
    return {};
  }
  if (!WLW_VERIFY(settings.layer < impl_->layer_count, "physics: layer %d of %d", settings.layer, impl_->layer_count)) {
    return {};
  }
  JPH::BodyCreationSettings create(shape->shape, ToJolt(settings.position),
                                   ToJolt(NormalizedOrIdentity(settings.rotation)), ToJolt(settings.motion),
                                   static_cast<JPH::ObjectLayer>(settings.layer));
  create.mFriction = settings.friction;
  create.mRestitution = settings.restitution;
  create.mLinearDamping = settings.linear_damping;
  create.mAngularDamping = settings.angular_damping;
  create.mGravityFactor = settings.gravity_factor;
  create.mMotionQuality = settings.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
  create.mAllowDynamicOrKinematic = settings.allow_motion_change;
  create.mAllowSleeping = settings.allow_sleeping;
  create.mUserData = settings.user_data;
  if (settings.mass > 0.0f) {
    create.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    create.mMassPropertiesOverride.mMass = settings.mass;
  }

  JPH::BodyInterface& bodies = impl_->Bodies();
  JPH::Body* body = bodies.CreateBody(create);
  if (!WLW_VERIFY(body != nullptr, "physics: the world is full (WorldSettings::max_bodies)")) {
    return {};
  }
  // Every body carries the pair filter, its index as the sub-group (joints block pairs by index).
  body->SetCollisionGroup(JPH::CollisionGroup(impl_->pairs.GetPtr(), 0, body->GetID().GetIndex()));
  const bool wake = settings.motion != MotionType::Static && settings.awake;
  bodies.AddBody(body->GetID(), wake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
  return ToWlw(body->GetID());
}

void World::DestroyBody(BodyId body) {
  if (!Contains(body)) {
    return;
  }
  for (uint32_t i = 0; i < impl_->joints.size(); ++i) {
    const Impl::Joint& joint = impl_->joints[i];
    if (joint.hinge != nullptr && (joint.a == body || joint.b == body)) {
      impl_->RemoveJointAt(i);
    }
  }
  JPH::BodyInterface& bodies = impl_->Bodies();
  bodies.RemoveBody(ToJolt(body));
  bodies.DestroyBody(ToJolt(body));
}

bool World::Contains(BodyId body) const { return body.IsValid() && impl_->Bodies().IsAdded(ToJolt(body)); }

Vec3 World::Position(BodyId body) const { return ToWlw(impl_->Bodies().GetPosition(ToJolt(body))); }
Quat World::Rotation(BodyId body) const {
  return Contains(body) ? ToWlw(impl_->Bodies().GetRotation(ToJolt(body))) : Quat(1.0f, 0.0f, 0.0f, 0.0f);
}
Mat4 World::Transform(BodyId body) const {
  Mat4 transform = glm::mat4_cast(Rotation(body));
  transform[3] = Vec4(Position(body), 1.0f);
  return transform;
}
Vec3 World::CenterOfMass(BodyId body) const { return ToWlw(impl_->Bodies().GetCenterOfMassPosition(ToJolt(body))); }

void World::SetPose(BodyId body, Vec3 position, Quat rotation) {
  if (!Contains(body)) {
    return;
  }
  const bool moving = impl_->Bodies().GetMotionType(ToJolt(body)) != JPH::EMotionType::Static;
  impl_->Bodies().SetPositionAndRotation(ToJolt(body), ToJolt(position), ToJolt(NormalizedOrIdentity(rotation)),
                                         moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

void World::MoveKinematic(BodyId body, Vec3 target_position, Quat target_rotation, float dt) {
  if (!Contains(body) || dt <= 0.0f) {
    return;
  }
  if (!WLW_VERIFY(impl_->Bodies().GetMotionType(ToJolt(body)) == JPH::EMotionType::Kinematic,
                  "physics: MoveKinematic on a body that isn't kinematic")) {
    return;
  }
  impl_->Bodies().MoveKinematic(ToJolt(body), ToJolt(target_position), ToJolt(NormalizedOrIdentity(target_rotation)),
                                dt);
}

Vec3 World::LinearVelocity(BodyId body) const { return ToWlw(impl_->Bodies().GetLinearVelocity(ToJolt(body))); }
Vec3 World::AngularVelocity(BodyId body) const { return ToWlw(impl_->Bodies().GetAngularVelocity(ToJolt(body))); }
Vec3 World::PointVelocity(BodyId body, Vec3 world_point) const {
  return ToWlw(impl_->Bodies().GetPointVelocity(ToJolt(body), ToJolt(world_point)));
}
void World::SetLinearVelocity(BodyId body, Vec3 velocity) {
  impl_->Bodies().SetLinearVelocity(ToJolt(body), ToJolt(velocity));
}
void World::SetAngularVelocity(BodyId body, Vec3 velocity) {
  impl_->Bodies().SetAngularVelocity(ToJolt(body), ToJolt(velocity));
}

void World::AddForce(BodyId body, Vec3 force) { impl_->Bodies().AddForce(ToJolt(body), ToJolt(force)); }
void World::AddForceAt(BodyId body, Vec3 force, Vec3 world_point) {
  impl_->Bodies().AddForce(ToJolt(body), ToJolt(force), ToJolt(world_point));
}
void World::AddTorque(BodyId body, Vec3 torque) { impl_->Bodies().AddTorque(ToJolt(body), ToJolt(torque)); }
void World::AddImpulse(BodyId body, Vec3 impulse) {
  if (GetMotionType(body) == MotionType::Dynamic) {
    impl_->Bodies().AddImpulse(ToJolt(body), ToJolt(impulse));
  }
}
void World::AddImpulseAt(BodyId body, Vec3 impulse, Vec3 world_point) {
  if (GetMotionType(body) == MotionType::Dynamic) {
    impl_->Bodies().AddImpulse(ToJolt(body), ToJolt(impulse), ToJolt(world_point));
  }
}

float World::Mass(BodyId body) const {
  const JPH::Body* found = impl_->Find(body);
  if (found == nullptr || found->GetMotionPropertiesUnchecked() == nullptr) {
    return 0.0f;
  }
  const float inverse = found->GetMotionPropertiesUnchecked()->GetInverseMassUnchecked();
  return inverse > 0.0f ? 1.0f / inverse : 0.0f;
}

MotionType World::GetMotionType(BodyId body) const {
  return Contains(body) ? ToWlw(impl_->Bodies().GetMotionType(ToJolt(body))) : MotionType::Static;
}

void World::SetMotionType(BodyId body, MotionType motion) {
  JPH::Body* found = impl_->Find(body);
  if (found == nullptr || ToWlw(found->GetMotionType()) == motion) {
    return;
  }
  if (!WLW_VERIFY(motion == MotionType::Static || found->CanBeKinematicOrDynamic(),
                  "physics: a static body made without allow_motion_change can't move")) {
    return;
  }
  impl_->Bodies().SetMotionType(ToJolt(body), ToJolt(motion), JPH::EActivation::Activate);
}

Layer World::GetLayer(BodyId body) const {
  return Contains(body) ? static_cast<Layer>(impl_->Bodies().GetObjectLayer(ToJolt(body))) : Layer{0};
}

void World::SetLayer(BodyId body, Layer layer) {
  if (!WLW_VERIFY(layer < impl_->layer_count, "physics: layer %d of %d", layer, impl_->layer_count)) {
    return;
  }
  impl_->Bodies().SetObjectLayer(ToJolt(body), static_cast<JPH::ObjectLayer>(layer));
}

void World::SetFriction(BodyId body, float friction) { impl_->Bodies().SetFriction(ToJolt(body), friction); }

bool World::IsAwake(BodyId body) const { return Contains(body) && impl_->Bodies().IsActive(ToJolt(body)); }

void World::SetAwake(BodyId body, bool awake) {
  if (!Contains(body) || GetMotionType(body) == MotionType::Static) {
    return;
  }
  if (awake) {
    impl_->Bodies().ActivateBody(ToJolt(body));
  } else {
    impl_->Bodies().DeactivateBody(ToJolt(body));
  }
}

uint64_t World::UserData(BodyId body) const { return Contains(body) ? impl_->Bodies().GetUserData(ToJolt(body)) : 0; }
void World::SetUserData(BodyId body, uint64_t value) { impl_->Bodies().SetUserData(ToJolt(body), value); }

// ---- Joints ----

JointId World::AddHinge(const HingeSettings& settings) {
  if (!WLW_VERIFY(Contains(settings.body_a) && Contains(settings.body_b) && settings.body_a != settings.body_b,
                  "physics: AddHinge needs two different bodies in the world")) {
    return {};
  }
  if (!WLW_VERIFY(
          GetMotionType(settings.body_a) != MotionType::Static || GetMotionType(settings.body_b) != MotionType::Static,
          "physics: AddHinge between two static bodies")) {
    return {};
  }
  const Vec3 axis = SafeNormalize(settings.axis, Vec3(0.0f));
  // The normals must be perpendicular to the axis: take out what lies along it.
  const Vec3 normal_a = SafeNormalize(settings.normal_a - axis * glm::dot(settings.normal_a, axis), Vec3(0.0f));
  const Vec3 normal_b = SafeNormalize(settings.normal_b - axis * glm::dot(settings.normal_b, axis), Vec3(0.0f));
  if (!WLW_VERIFY(glm::length(axis) > 0.5f && glm::length(normal_a) > 0.5f && glm::length(normal_b) > 0.5f,
                  "physics: AddHinge needs an axis and normals across it")) {
    return {};
  }
  JPH::HingeConstraintSettings hinge;
  hinge.mSpace = JPH::EConstraintSpace::WorldSpace;
  hinge.mPoint1 = hinge.mPoint2 = ToJolt(settings.point);
  hinge.mHingeAxis1 = hinge.mHingeAxis2 = ToJolt(axis);
  hinge.mNormalAxis1 = ToJolt(normal_a);
  hinge.mNormalAxis2 = ToJolt(normal_b);
  hinge.mLimitsMin = std::clamp(settings.min_angle, -kPi, 0.0f);
  hinge.mLimitsMax = std::clamp(settings.max_angle, 0.0f, kPi);
  hinge.mMaxFrictionTorque = std::max(settings.friction_torque, 0.0f);

  JPH::BodyInterface& bodies = impl_->Bodies();
  auto* constraint = static_cast<JPH::HingeConstraint*>(
      bodies.CreateConstraint(&hinge, ToJolt(settings.body_a), ToJolt(settings.body_b)));
  if (!WLW_VERIFY(constraint != nullptr, "physics: AddHinge failed")) {
    return {};
  }
  impl_->system.AddConstraint(constraint);
  bodies.ActivateConstraint(constraint);

  uint32_t index;
  if (!impl_->free_joints.empty()) {
    index = impl_->free_joints.back();
    impl_->free_joints.pop_back();
  } else {
    index = static_cast<uint32_t>(impl_->joints.size());
    impl_->joints.emplace_back();
  }
  Impl::Joint& joint = impl_->joints[index];
  joint.hinge = constraint;
  joint.a = settings.body_a;
  joint.b = settings.body_b;
  joint.blocks_pair = !settings.collide_connected;
  if (joint.blocks_pair) {
    impl_->pairs->Block(ToJolt(joint.a).GetIndex(), ToJolt(joint.b).GetIndex());
  }
  ++impl_->joint_count;
  return JointId{index, joint.generation};
}

void World::RemoveJoint(JointId joint) {
  if (impl_->FindJoint(joint) != nullptr) {
    impl_->RemoveJointAt(joint.index);
  }
}

bool World::Contains(JointId joint) const { return impl_->FindJoint(joint) != nullptr; }

HingeState World::GetHinge(JointId joint) const {
  const Impl::Joint* found = impl_->FindJoint(joint);
  if (found == nullptr) {
    return {};
  }
  const JPH::HingeConstraint& hinge = *found->hinge;
  const JPH::Body& a = *hinge.GetBody1();
  const JPH::Body& b = *hinge.GetBody2();
  const JPH::RMat44 a_transform = a.GetCenterOfMassTransform();
  const JPH::Vec3 axis = a_transform.Multiply3x3(hinge.GetLocalSpaceHingeAxis1());

  HingeState state;
  state.angle = hinge.GetCurrentAngle();
  state.speed = (b.GetAngularVelocity() - a.GetAngularVelocity()).Dot(axis);
  state.point = ToWlw(a_transform * hinge.GetLocalSpacePoint1());
  state.axis = ToWlw(axis);
  const float per_second = impl_->last_substep_dt > 0.0f ? 1.0f / impl_->last_substep_dt : 0.0f;
  state.force = ToWlw(hinge.GetTotalLambdaPosition()) * per_second;
  const JPH::Vector<2> bend = hinge.GetTotalLambdaRotation();
  state.torque = std::sqrt(bend[0] * bend[0] + bend[1] * bend[1]) * per_second;
  state.limit_torque = std::abs(hinge.GetTotalLambdaRotationLimits()) * per_second;
  return state;
}

void World::SetHingeFriction(JointId joint, float friction_torque) {
  if (const Impl::Joint* found = impl_->FindJoint(joint)) {
    found->hinge->SetMaxFrictionTorque(std::max(friction_torque, 0.0f));
  }
}

void World::SetHingeLimits(JointId joint, float min_angle, float max_angle) {
  if (const Impl::Joint* found = impl_->FindJoint(joint)) {
    found->hinge->SetLimits(std::clamp(min_angle, -kPi, 0.0f), std::clamp(max_angle, 0.0f, kPi));
  }
}

std::pair<BodyId, BodyId> World::JointBodies(JointId joint) const {
  const Impl::Joint* found = impl_->FindJoint(joint);
  return found != nullptr ? std::pair{found->a, found->b} : std::pair<BodyId, BodyId>{};
}

// ---- Queries ----

std::optional<RayHit> World::CastRay(Vec3 origin, Vec3 direction, float max_distance, const QueryFilter& filter) const {
  const Vec3 unit = SafeNormalize(direction, Vec3(0.0f));
  if (max_distance <= 0.0f || glm::length(unit) < 0.5f) {
    return std::nullopt;
  }
  const JPH::RRayCast ray{ToJolt(origin), ToJolt(unit * max_distance)};
  JPH::RayCastResult result;
  const MaskBroadPhaseFilter broad_phase(filter.layers);
  const MaskObjectFilter layers(filter.layers);
  const QueryBodyFilter bodies(filter);
  if (!impl_->system.GetNarrowPhaseQueryNoLock().CastRay(ray, result, broad_phase, layers, bodies)) {
    return std::nullopt;
  }
  RayHit hit;
  hit.body = ToWlw(result.mBodyID);
  hit.distance = result.mFraction * max_distance;
  hit.point = origin + unit * hit.distance;
  if (const JPH::Body* body = impl_->Find(hit.body)) {
    hit.normal = ToWlw(body->GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ToJolt(hit.point)));
  }
  return hit;
}

// ---- Stats ----

int World::BodyCount() const { return static_cast<int>(impl_->system.GetNumBodies()); }
int World::AwakeBodyCount() const {
  return static_cast<int>(impl_->system.GetNumActiveBodies(JPH::EBodyType::RigidBody));
}
int World::JointCount() const { return impl_->joint_count; }
float World::LastStepMs() const { return impl_->last_step_ms; }

}  // namespace wlw::physics
