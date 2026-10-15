#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/glm.hpp>

#include "wlw/core/vector3.h"

namespace wlw::scene {

// A half-line: every point origin + t * direction with t >= 0. `direction` is unit length.
// Typical source: Camera3D::ScreenPointToRay (what is under the finger/cursor?).
struct Ray {
  glm::vec3 origin{0.0f};
  glm::vec3 direction{0.0f, 0.0f, -1.0f};

  glm::vec3 At(float t) const { return origin + t * direction; }
};

// The points p with dot(normal, p) + distance == 0. `normal` is unit length; `distance` is the
// signed offset from the origin, so the ground plane y = 0 is {{0, 1, 0}, 0} and y = h is
// {{0, 1, 0}, -h}. Points on the normal's side have a positive GetDistanceToPoint().
struct Plane {
  glm::vec3 normal;
  float distance;

  static Plane FromPointAndNormal(const glm::vec3& point, const glm::vec3& normal) {
    return {normal, -glm::dot(normal, point)};
  }

  float GetDistanceToPoint(const glm::vec3& point) const { return glm::dot(normal, point) + distance; }
};

// Where the ray hits the plane, or nothing if it runs parallel to it or the plane is behind it.
// Derivation: put origin + t * direction into the plane equation and solve for t:
//   dot(n, o + t d) + dist = 0   =>   t = -(dot(n, o) + dist) / dot(n, d)
inline std::optional<glm::vec3> IntersectPlane(const Ray& ray, const Plane& plane) {
  constexpr float kParallel = 1e-6f;
  const float facing = glm::dot(plane.normal, ray.direction);
  if (std::abs(facing) < kParallel) return std::nullopt;  // parallel: no hit (or the ray lies in it)
  // Same value as `facing` here, but never 0 even in code the early return skips: MSVC's C4723 ("potential
  // divide by 0") fires on an inlined constant-0 `facing` despite the return above.
  const float divisor = std::copysign(std::max(std::abs(facing), kParallel), facing);
  const float t = -plane.GetDistanceToPoint(ray.origin) / divisor;
  if (t < 0.0f) return std::nullopt;  // the plane is behind the ray's start
  return ray.At(t);
}

struct AABB {
  // ... existing struct members ...
  core::Vector3 min;
  core::Vector3 max;

  bool Intersects(const AABB& other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) && (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
  }

  static AABB FromPositionAndSize(const core::Vector3& pos, float size) {
    float half = size / 2.0f;
    return {{pos.x - half, pos.y - half, pos.z - half}, {pos.x + half, pos.y + half, pos.z + half}};
  }

  static AABB FromPositionAndScale(const core::Vector3& pos, const core::Vector3& scale) {
    core::Vector3 half = {scale.x / 2.0f, scale.y / 2.0f, scale.z / 2.0f};
    return {{pos.x - half.x, pos.y - half.y, pos.z - half.z}, {pos.x + half.x, pos.y + half.y, pos.z + half.z}};
  }
};

// The box around `box` moved by the affine `transform` (exactly the box of its 8 transformed corners).
// Arvo's method ("Transforming Axis-Aligned Bounding Boxes", Graphics Gems 1990): the centre goes
// through the matrix, the half-size through the matrix's absolute values (each output axis gets the
// most every input axis can reach along it). 2 matrix-vector products instead of 8.
inline AABB TransformBox(const glm::mat4& transform, const AABB& box) {
  const glm::vec3 min(box.min.x, box.min.y, box.min.z);
  const glm::vec3 max(box.max.x, box.max.y, box.max.z);
  const glm::vec3 centre = (min + max) * 0.5f;
  const glm::vec3 half = (max - min) * 0.5f;
  const glm::mat3 linear(transform);
  const glm::vec3 moved = linear * centre + glm::vec3(transform[3]);
  const glm::mat3 reach(glm::abs(linear[0]), glm::abs(linear[1]), glm::abs(linear[2]));
  const glm::vec3 extent = reach * half;
  return {{moved.x - extent.x, moved.y - extent.y, moved.z - extent.z},
          {moved.x + extent.x, moved.y + extent.y, moved.z + extent.z}};
}

// The smallest box holding both.
inline AABB Union(const AABB& a, const AABB& b) {
  return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
          {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)}};
}

}  // namespace wlw::scene
