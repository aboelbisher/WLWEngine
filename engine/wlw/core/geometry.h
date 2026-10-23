#pragma once

// Shapes and the tests between them, in one namespace (like Godot's AABB / Plane / Rect2, Unity's Bounds / Ray,
// Unreal's FBox / FSphere / FPlane): culling, picking, shadow fitting, editor gizmos, game hit tests.
//   Overlaps(a, b)    do two shapes touch? (touching edges count)
//   Raycast(ray, s)   how far along the ray it first meets the shape (0 when it starts inside), or nothing
// Boxes and rects are closed: a point on the edge is inside.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include "wlw/core/deprecated.h"
#include "wlw/core/math.h"

namespace wlw {

// A half-line: every point origin + t * direction with t >= 0. `direction` is unit length.
// Typical source: Camera3D::ScreenPointToRay (what is under the finger/cursor?).
struct Ray {
  Vec3 origin{0.0f};
  Vec3 direction{0.0f, 0.0f, -1.0f};

  Vec3 At(float t) const { return origin + t * direction; }
};

// The points p with dot(normal, p) + distance == 0. `normal` is unit length; `distance` is the
// signed offset from the origin, so the ground plane y = 0 is {{0, 1, 0}, 0} and y = h is
// {{0, 1, 0}, -h}. Points on the normal's side have a positive GetDistanceToPoint().
struct Plane {
  Vec3 normal{0.0f, 1.0f, 0.0f};
  float distance = 0.0f;

  static Plane FromPointAndNormal(const Vec3& point, const Vec3& normal) { return {normal, -glm::dot(normal, point)}; }
  // Through three points; the normal faces the side they are counter-clockwise from (the right-hand rule). They
  // must not be on one line.
  static Plane FromPoints(const Vec3& a, const Vec3& b, const Vec3& c) {
    return FromPointAndNormal(a, glm::normalize(glm::cross(b - a, c - a)));
  }

  float GetDistanceToPoint(const Vec3& point) const { return glm::dot(normal, point) + distance; }
};

// An axis-aligned box. {} is the point box at the origin; Empty() is the box with nothing in it, to grow with
// Expanded() / Union().
struct AABB {
  Vec3 min{0.0f};
  Vec3 max{0.0f};

  static AABB FromCenterSize(const Vec3& center, const Vec3& size) { return FromCenterExtents(center, size * 0.5f); }
  static AABB FromCenterExtents(const Vec3& center, const Vec3& extents) {
    return {center - extents, center + extents};
  }
  // min = +inf, max = -inf: holds nothing, and Union / Expanded with it give the other box / the point.
  static constexpr AABB Empty() {
    constexpr float kInf = std::numeric_limits<float>::infinity();
    return {Vec3(kInf), Vec3(-kInf)};
  }
  bool IsEmpty() const { return min.x > max.x || min.y > max.y || min.z > max.z; }

  Vec3 Center() const { return (min + max) * 0.5f; }
  Vec3 Size() const { return max - min; }
  Vec3 Extents() const { return (max - min) * 0.5f; }  // half the size

  bool Contains(const Vec3& p) const {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
  }
  // `by` added on every side (negative shrinks).
  AABB Grown(float by) const { return {min - Vec3(by), max + Vec3(by)}; }
  // The box grown just enough to hold `p`.
  AABB Expanded(const Vec3& p) const { return {glm::min(min, p), glm::max(max, p)}; }

#if !WLW_NO_DEPRECATED
  WLW_DEPRECATED(0.3, "Use Overlaps(a, b)") bool Intersects(const AABB& other) const {
    return glm::all(glm::lessThanEqual(min, other.max)) && glm::all(glm::greaterThanEqual(max, other.min));
  }
  WLW_DEPRECATED(0.3, "Use AABB::FromCenterSize(pos, Vec3(size))")
  static AABB FromPositionAndSize(const Vec3& pos, float size) { return FromCenterSize(pos, Vec3(size)); }
  WLW_DEPRECATED(0.3, "Use AABB::FromCenterSize(pos, scale)")
  static AABB FromPositionAndScale(const Vec3& pos, const Vec3& scale) { return FromCenterSize(pos, scale); }
#endif
};

struct Sphere {
  Vec3 center{0.0f};
  float radius = 0.0f;

  bool Contains(const Vec3& p) const { return glm::dot(p - center, p - center) <= radius * radius; }
};

// The sphere through the box's corners (centred on it): loose, but quick.
inline Sphere BoundingSphere(const AABB& box) { return {box.Center(), glm::length(box.Extents())}; }

// A 2D axis-aligned rectangle (UI, 2D games, top-down boards). Same layout as ui::Rect, which is this type.
struct Rect {
  Vec2 min{0.0f};
  Vec2 max{0.0f};

  static Rect FromCenterSize(const Vec2& center, const Vec2& size) {
    return {center - size * 0.5f, center + size * 0.5f};
  }
  static Rect FromPositionSize(const Vec2& position, const Vec2& size) { return {position, position + size}; }

  Vec2 Center() const { return (min + max) * 0.5f; }
  Vec2 Size() const { return max - min; }
  bool Contains(const Vec2& p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
  Rect Grown(float by) const { return {min - Vec2(by), max + Vec2(by)}; }
  Rect Expanded(const Vec2& p) const { return {glm::min(min, p), glm::max(max, p)}; }
};

// The six planes of a view volume, normals pointing inwards: Left, Right, Bottom, Top, Near, Far.
struct Frustum {
  std::array<Plane, 6> planes;

  // From a projection * view matrix (GL clip space, z in -1..1): Gribb and Hartmann's method, each plane a sum or
  // difference of the matrix's rows, normalized.
  static Frustum FromMatrix(const Mat4& m) {
    Frustum f;
    const Vec4 row_x(m[0][0], m[1][0], m[2][0], m[3][0]);
    const Vec4 row_y(m[0][1], m[1][1], m[2][1], m[3][1]);
    const Vec4 row_z(m[0][2], m[1][2], m[2][2], m[3][2]);
    const Vec4 row_w(m[0][3], m[1][3], m[2][3], m[3][3]);
    const Vec4 sides[6] = {row_w + row_x, row_w - row_x, row_w + row_y, row_w - row_y, row_w + row_z, row_w - row_z};
    for (int i = 0; i < 6; ++i) {
      const float length = glm::length(Vec3(sides[i]));
      f.planes[i] = {Vec3(sides[i]) / length, sides[i].w / length};
    }
    return f;
  }

#if !WLW_NO_DEPRECATED
  WLW_DEPRECATED(0.3, "Use Overlaps(frustum, box)") bool TestAABB(const AABB& box) const;
#endif
};

// ---- Overlaps ----

inline bool Overlaps(const AABB& a, const AABB& b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z &&
         a.max.z >= b.min.z;
}
inline bool Overlaps(const Rect& a, const Rect& b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y;
}
inline bool Overlaps(const Sphere& a, const Sphere& b) {
  const float reach = a.radius + b.radius;
  return glm::dot(a.center - b.center, a.center - b.center) <= reach * reach;
}
// Exact: the box's point nearest the centre is within the radius (Arvo).
inline bool Overlaps(const AABB& box, const Sphere& sphere) {
  const Vec3 nearest = glm::clamp(sphere.center, box.min, box.max);
  return glm::dot(nearest - sphere.center, nearest - sphere.center) <= sphere.radius * sphere.radius;
}
inline bool Overlaps(const Sphere& sphere, const AABB& box) { return Overlaps(box, sphere); }

// Culling tests: false only when the shape is wholly outside one plane, so they never drop a visible shape; a
// shape near a corner of the volume can pass while outside it (a few extra draws, never a missing one).
// The box test checks each plane's "positive vertex" (the corner furthest along the normal).
inline bool Overlaps(const Frustum& frustum, const AABB& box) {
  for (const Plane& plane : frustum.planes) {
    const Vec3 furthest(plane.normal.x >= 0.0f ? box.max.x : box.min.x, plane.normal.y >= 0.0f ? box.max.y : box.min.y,
                        plane.normal.z >= 0.0f ? box.max.z : box.min.z);
    if (plane.GetDistanceToPoint(furthest) < 0.0f) return false;
  }
  return true;
}
inline bool Overlaps(const Frustum& frustum, const Sphere& sphere) {
  for (const Plane& plane : frustum.planes) {
    if (plane.GetDistanceToPoint(sphere.center) < -sphere.radius) return false;
  }
  return true;
}

#if !WLW_NO_DEPRECATED
inline bool Frustum::TestAABB(const AABB& box) const { return Overlaps(*this, box); }
#endif

// ---- Raycasts: the distance t along the ray (ray.At(t) is the point), or nothing ----

// Nothing when the ray runs parallel to the plane or the plane is behind it.
// Derivation: put origin + t * direction into the plane equation and solve for t:
//   dot(n, o + t d) + dist = 0   =>   t = -(dot(n, o) + dist) / dot(n, d)
inline std::optional<float> Raycast(const Ray& ray, const Plane& plane) {
  constexpr float kParallel = 1e-6f;
  const float facing = glm::dot(plane.normal, ray.direction);
  if (std::abs(facing) < kParallel) return std::nullopt;  // parallel: no hit (or the ray lies in it)
  // Same value as `facing` here, but never 0 even in code the early return skips: MSVC's C4723 ("potential
  // divide by 0") fires on an inlined constant-0 `facing` despite the return above.
  const float divisor = std::copysign(std::max(std::abs(facing), kParallel), facing);
  const float t = -plane.GetDistanceToPoint(ray.origin) / divisor;
  if (t < 0.0f) return std::nullopt;  // the plane is behind the ray's start
  return t;
}

// The slab method (Kay and Kajiya): the ray is inside the box while it is between all three pairs of planes.
// A direction component of 0 is handled (the ray then stays inside that slab or misses it), with no 0 * inf.
inline std::optional<float> Raycast(const Ray& ray, const AABB& box) {
  float near_t = 0.0f;
  float far_t = std::numeric_limits<float>::infinity();
  for (int axis = 0; axis < 3; ++axis) {
    const float o = ray.origin[axis];
    const float d = ray.direction[axis];
    if (d == 0.0f) {
      if (o < box.min[axis] || o > box.max[axis]) return std::nullopt;
      continue;
    }
    float t0 = (box.min[axis] - o) / d;
    float t1 = (box.max[axis] - o) / d;
    if (t0 > t1) std::swap(t0, t1);
    near_t = std::max(near_t, t0);
    far_t = std::min(far_t, t1);
    if (near_t > far_t) return std::nullopt;
  }
  return near_t;
}

inline std::optional<float> Raycast(const Ray& ray, const Sphere& sphere) {
  // |o + t d - c|^2 = r^2 with |d| = 1:  t^2 + 2 b t + c = 0,  b = dot(o - c, d),  c = |o - c|^2 - r^2.
  const Vec3 to_origin = ray.origin - sphere.center;
  const float b = glm::dot(to_origin, ray.direction);
  const float c = glm::dot(to_origin, to_origin) - sphere.radius * sphere.radius;
  if (c <= 0.0f) return 0.0f;         // starts inside
  if (b > 0.0f) return std::nullopt;  // outside and pointing away
  const float discriminant = b * b - c;
  if (discriminant < 0.0f) return std::nullopt;  // passes by
  return -b - std::sqrt(discriminant);
}

// Möller and Trumbore's test against the triangle a, b, c (both sides; editor picking, mesh hits).
inline std::optional<float> Raycast(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c) {
  constexpr float kParallel = 1e-8f;
  const Vec3 edge1 = b - a;
  const Vec3 edge2 = c - a;
  const Vec3 p = glm::cross(ray.direction, edge2);
  const float det = glm::dot(edge1, p);
  if (std::abs(det) < kParallel) return std::nullopt;  // parallel to the triangle's plane (or degenerate)
  const float inverse = 1.0f / det;
  const Vec3 s = ray.origin - a;
  const float u = glm::dot(s, p) * inverse;
  if (u < 0.0f || u > 1.0f) return std::nullopt;
  const Vec3 q = glm::cross(s, edge1);
  const float v = glm::dot(ray.direction, q) * inverse;
  if (v < 0.0f || u + v > 1.0f) return std::nullopt;
  const float t = glm::dot(edge2, q) * inverse;
  if (t < 0.0f) return std::nullopt;
  return t;
}

// ---- Boxes ----

// The box around `box` moved by the affine `transform` (exactly the box of its 8 transformed corners).
// Arvo's method ("Transforming Axis-Aligned Bounding Boxes", Graphics Gems 1990): the centre goes
// through the matrix, the half-size through the matrix's absolute values (each output axis gets the
// most every input axis can reach along it). 2 matrix-vector products instead of 8.
inline AABB TransformBox(const Mat4& transform, const AABB& box) {
  const Vec3 centre = (box.min + box.max) * 0.5f;
  const Vec3 half = (box.max - box.min) * 0.5f;
  const Mat3 linear(transform);
  const Vec3 moved = linear * centre + Vec3(transform[3]);
  const Mat3 reach(glm::abs(linear[0]), glm::abs(linear[1]), glm::abs(linear[2]));
  const Vec3 extent = reach * half;
  return {moved - extent, moved + extent};
}

// The smallest box / rect holding both.
inline AABB Union(const AABB& a, const AABB& b) { return {glm::min(a.min, b.min), glm::max(a.max, b.max)}; }
inline Rect Union(const Rect& a, const Rect& b) { return {glm::min(a.min, b.min), glm::max(a.max, b.max)}; }

}  // namespace wlw
