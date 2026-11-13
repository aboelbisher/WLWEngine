#pragma once

#include <memory>
#include <span>

#include "wlw/core/geometry.h"
#include "wlw/core/math.h"

namespace wlw::physics {

// A collision shape: immutable, shared by every body made with it (copies are cheap and share it, like Jolt's
// RefConst<Shape> or Godot's Ref<Shape3D>). Made by the factories below; a default Shape is empty (IsValid false) and
// a body can't be made from it. Shapes are in the body's own space: its origin is the body's position.
// Any thread may make shapes; a World only reads them.
//
//   physics::Shape tile = physics::Shape::ConvexHull(corners, 0.02f);
//   physics::Shape crate = physics::Shape::Box({0.5f, 0.5f, 0.5f});
//
// Convex radius: the shape's corners and edges are rounded by this much (inside the size given), which makes
// collisions cheaper and steadier; Jolt's default is 5 cm. It is capped at what fits the shape.
class Shape {
 public:
  Shape() = default;

  // A box centred on the origin, half_extents along x, y, z (each > 0).
  static Shape Box(Vec3 half_extents, float convex_radius = 0.05f);
  // A sphere centred on the origin.
  static Shape Sphere(float radius);
  // The convex hull of the points (at least 4, not all in one plane). Empty (logged) when they make no hull.
  static Shape ConvexHull(std::span<const Vec3> points, float convex_radius = 0.05f);

  bool IsValid() const { return impl_ != nullptr; }
  // The box around the shape in its own space (zero box when empty).
  AABB LocalBounds() const;
  // Its volume (m^3) and centre of mass in its own space: a body's default mass is the volume x 1000 kg/m^3.
  float Volume() const;
  Vec3 CenterOfMass() const;

  friend bool operator==(const Shape& a, const Shape& b) { return a.impl_ == b.impl_; }

  class InternalAccess;  // wlw::physics's own code only

 private:
  struct Impl;  // the Jolt shape (physics/jolt_internal.h)
  explicit Shape(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
  std::shared_ptr<const Impl> impl_;
};

class Shape::InternalAccess {
 public:
  using Impl = Shape::Impl;
  static Shape Make(std::shared_ptr<const Impl> impl) { return Shape(std::move(impl)); }
  static const Impl* Get(const Shape& shape) { return shape.impl_.get(); }
};

}  // namespace wlw::physics
