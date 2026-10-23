#include "wlw/scene/camera_controllers.h"

#include <algorithm>
#include <cmath>

#include "wlw/core/rotation.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/node.h"

namespace wlw::scene {
namespace {

// The share of the way to a goal covered in `dt` at `rate` (1/s): ExpDecay's factor, the same over one step or many.
// rate <= 0: all of it.
float Approach(float rate, float dt) { return rate > 0.0f ? 1.0f - std::exp(-rate * dt) : 1.0f; }

Quat Heading(float yaw, float pitch) { return QuatFromEuler({pitch, 0.0f, yaw}, EulerOrder::ZXY); }

}  // namespace

// ---- Fly ----

void FlyController::Attach(const Node3D& camera) {
  const Vec3 angles = EulerFromQuat(camera.GetWorldRotation(), EulerOrder::ZXY);
  pitch_ = std::clamp(angles.x, -pitch_limit, pitch_limit);
  yaw_ = angles.z;
  velocity_ = Vec3(0.0f);
}

void FlyController::Update(Node3D& camera, const FlyInput& input, float dt) {
  yaw_ = WrapAngle(yaw_ - input.look.x);  // turning right = clockwise seen from above = negative about +Z
  pitch_ = std::clamp(pitch_ + input.look.y, -pitch_limit, pitch_limit);
  const Quat rotation = Heading(yaw_, pitch_);
  camera.SetWorldRotation(rotation);

  Vec3 move = input.move;
  if (glm::dot(move, move) > 1.0f) move = glm::normalize(move);  // diagonals no faster
  const Vec3 wanted = rotation * move * speed * (input.boost ? boost_multiplier : 1.0f);
  velocity_ = damping > 0.0f ? ExpDecay(velocity_, wanted, damping, dt) : wanted;
  camera.TranslateWorld(velocity_ * dt);
}

// ---- Orbit ----

void OrbitController::Apply(Camera3D& camera) const {
  const Quat rotation = Heading(yaw_now_, pitch_now_);
  camera.SetWorldRotation(rotation);
  camera.SetWorldPosition(pivot_now_ + rotation * Vec3(0.0f, -distance_now_, 0.0f));
}

void OrbitController::Snap(Camera3D& camera) {
  pitch = std::clamp(pitch, -pitch_limit, pitch_limit);
  distance = std::clamp(distance, min_distance, max_distance);
  pivot_now_ = pivot;
  yaw_now_ = yaw;
  pitch_now_ = pitch;
  distance_now_ = distance;
  if (size_goal_ > 0.0f) camera.SetSize(size_goal_);
  started_ = true;
  Apply(camera);
}

void OrbitController::Frame(const wlw::AABB& box, Camera3D& camera, float aspect) {
  pivot = box.Center();
  const float radius = std::max(0.5f * glm::length(box.max - box.min), 1e-4f);
  // Half the visible extents at distance 1 (orthographic: at any distance), the narrower side.
  const Vec2 visible = camera.VisibleSize(aspect, 1.0f);
  const float half = 0.5f * std::min(visible.x, visible.y);
  if (camera.IsOrthographic()) {
    size_goal_ = camera.GetSize() * radius / half;
    distance = std::max(distance, 2.0f * radius + camera.GetNear());  // the whole box in front of the near plane
  } else {
    // The sphere around the box touches the narrower side of the view cone: sin(half angle) = radius / distance.
    distance = radius / std::sin(std::atan(half));
  }
  distance = std::clamp(distance, min_distance, max_distance);
}

void OrbitController::Update(Camera3D& camera, const OrbitInput& input, float dt) {
  if (!started_) Snap(camera);

  yaw += input.rotate.x;    // the camera circles to its right
  pitch -= input.rotate.y;  // and rises (a negative pitch looks down)
  pitch = std::clamp(pitch, -pitch_limit, pitch_limit);
  if (input.pan.x != 0.0f || input.pan.y != 0.0f) {
    const Quat rotation = Heading(yaw_now_, pitch_now_);
    const float height = camera.VisibleSize(input.aspect, distance_now_).y;
    pivot += (rotation * Vec3(input.pan.x, 0.0f, input.pan.y)) * height;
  }
  if (input.zoom != 0.0f) {
    const float factor = std::pow(zoom_step, input.zoom);
    if (camera.IsOrthographic()) {
      size_goal_ = (size_goal_ > 0.0f ? size_goal_ : camera.GetSize()) / factor;
    } else {
      distance /= factor;
    }
  }
  distance = std::clamp(distance, min_distance, max_distance);

  const float t = Approach(smoothing, dt);
  pivot_now_ = Lerp(pivot_now_, pivot, t);
  yaw_now_ = Lerp(yaw_now_, yaw, t);
  pitch_now_ = Lerp(pitch_now_, pitch, t);
  distance_now_ = Lerp(distance_now_, distance, t);
  if (size_goal_ > 0.0f) camera.SetSize(Lerp(camera.GetSize(), size_goal_, t));
  Apply(camera);
}

// ---- Follow ----

void FollowController::Place(Node3D& camera, const Node3D& target, float position_t, float rotation_t) const {
  const Vec3 target_position = target.GetWorldPosition();
  const Vec3 wanted = target_position + (offset_in_target_space ? target.GetWorldRotation() * offset : offset);
  const Vec3 position = Lerp(camera.GetWorldPosition(), wanted, position_t);
  camera.SetWorldPosition(position);
  const Vec3 look = target_position + look_offset - position;
  if (glm::dot(look, look) < 1e-12f) return;
  const Quat facing = LookRotation(look, up);
  camera.SetWorldRotation(rotation_t >= 1.0f ? facing : glm::slerp(camera.GetWorldRotation(), facing, rotation_t));
}

void FollowController::Update(Node3D& camera, const Node3D& target, float dt) const {
  Place(camera, target, Approach(position_lag, dt), Approach(rotation_lag, dt));
}

void FollowController::Snap(Node3D& camera, const Node3D& target) const { Place(camera, target, 1.0f, 1.0f); }

}  // namespace wlw::scene
