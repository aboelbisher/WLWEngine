#pragma once

#include "wlw/core/geometry.h"
#include "wlw/core/math.h"

namespace wlw::scene {

class Node3D;
class Camera3D;

// Ways to move a camera, as plain objects a System updates each frame (nodes don't run code in this engine). They
// take their input as small structs, so the game (or the editor, Roadmap 5.1) decides which keys and gestures feed
// them, and tests run them without a window. Every smoothing is frame-rate independent (ExpDecay): one 0.1 s step
// lands where ten 0.01 s steps do. Like Godot's editor fly/orbit cameras and Unity's Cinemachine follow.

// ---- Fly: free movement, like a first-person camera or the editor's right-mouse fly mode ----
struct FlyInput {
  Vec3 move{0.0f};  // along the camera's axes, each -1..1: x = right, y = forward, z = up
  Vec2 look{0.0f};  // turn in radians this frame: x = to the right, y = up
  bool boost = false;
};

class FlyController {
 public:
  float speed = 5.0f;                     // m/s
  float boost_multiplier = 4.0f;          // speed x this while boosting
  float damping = 12.0f;                  // how fast the velocity follows the input (1/s; 0 = instantly)
  float pitch_limit = 89.0f * kDegToRad;  // looking up / down stops here (radians)

  // Takes the camera's current heading (yaw and pitch) so the first Update doesn't snap it.
  void Attach(const Node3D& camera);
  // Turns and moves `camera` (its world transform): yaw about world +Z, pitch about its own X, no roll.
  void Update(Node3D& camera, const FlyInput& input, float dt);

  float Yaw() const { return yaw_; }
  float Pitch() const { return pitch_; }
  const Vec3& Velocity() const { return velocity_; }

 private:
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  Vec3 velocity_{0.0f};
};

// ---- Orbit: around a pivot, like a model viewer or the editor's default navigation ----
struct OrbitInput {
  Vec2 rotate{0.0f};    // radians this frame: x = orbit to the right, y = up
  Vec2 pan{0.0f};       // move the pivot across the view, in fractions of the visible height (a drag across the
                        // whole screen height = 1): x = right, y = up
  float zoom = 0.0f;    // steps (a mouse wheel's notches): > 0 closer
  float aspect = 1.0f;  // the viewport's width / height (for pan)
};

class OrbitController {
 public:
  Vec3 pivot{0.0f};
  float yaw = 0.0f;        // radians about world +Z (0 = the camera on -Y of the pivot, looking along +Y)
  float pitch = -0.4f;     // radians (negative = looking down at the pivot)
  float distance = 10.0f;  // from the pivot
  float min_distance = 0.05f;
  float max_distance = 10000.0f;
  float zoom_step = 1.15f;  // distance (orthographic: size) divided by this per zoom step
  float pitch_limit = 89.0f * kDegToRad;
  float smoothing = 20.0f;  // how fast the camera follows (1/s; 0 = immediately)

  // Changes pivot / distance (or the orthographic size) so the box fits in view (`aspect` = the viewport's);
  // the camera glides there.
  void Frame(const wlw::AABB& box, Camera3D& camera, float aspect);
  // Jumps to the goal (no glide): after setting the public fields, or at the start.
  void Snap(Camera3D& camera);
  void Update(Camera3D& camera, const OrbitInput& input, float dt);

 private:
  void Apply(Camera3D& camera) const;

  bool started_ = false;
  Vec3 pivot_now_{0.0f};
  float yaw_now_ = 0.0f, pitch_now_ = 0.0f, distance_now_ = 10.0f;
  float size_goal_ = 0.0f;  // orthographic size goal (0 = the camera's own until a zoom)
};

// ---- Follow: trails a target node, like a third-person or chase camera ----
class FollowController {
 public:
  Vec3 offset{0.0f, -10.0f, 5.0f};      // where the camera sits relative to the target (behind and above)
  bool offset_in_target_space = false;  // true: the offset turns with the target (behind a car); false: world axes
  Vec3 look_offset{0.0f};               // the point looked at = the target's position + this (world axes)
  Vec3 up = kUp;
  float position_lag = 8.0f;  // how fast the camera catches up (1/s; 0 = rigidly attached)
  float rotation_lag = 0.0f;  // how fast it turns to the target (1/s; 0 = always looking straight at it)

  // Moves `camera` from where it is towards its place behind `target`, then turns it to look at the target.
  void Update(Node3D& camera, const Node3D& target, float dt) const;
  // Puts it in place at once (a cut).
  void Snap(Node3D& camera, const Node3D& target) const;

 private:
  void Place(Node3D& camera, const Node3D& target, float position_t, float rotation_t) const;
};

}  // namespace wlw::scene
