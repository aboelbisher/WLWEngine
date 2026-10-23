#pragma once

// Deprecated in 0.3: the geometry moved to wlw/core/geometry.h, namespace wlw (wlw::AABB, wlw::Ray, ...), with
// the intersection tests as Overlaps() / Raycast(). These names forward to it until games move (Roadmap 2.8).
// The engine itself never includes this header.
#include <optional>

#include "wlw/core/deprecated.h"
#include "wlw/core/geometry.h"

#if !WLW_NO_DEPRECATED
namespace wlw::scene {

using AABB WLW_DEPRECATED(0.3, "Use wlw::AABB (wlw/core/geometry.h)") = wlw::AABB;
using Ray WLW_DEPRECATED(0.3, "Use wlw::Ray (wlw/core/geometry.h)") = wlw::Ray;
using Plane WLW_DEPRECATED(0.3, "Use wlw::Plane (wlw/core/geometry.h)") = wlw::Plane;
using Frustum WLW_DEPRECATED(0.3, "Use wlw::Frustum (wlw/core/geometry.h)") = wlw::Frustum;

// Where the ray hits the plane, or nothing if it runs parallel to it or the plane is behind it.
WLW_DEPRECATED(0.3, "Use Raycast(ray, plane) (the distance; ray.At(t) is the point)")
inline std::optional<glm::vec3> IntersectPlane(const wlw::Ray& ray, const wlw::Plane& plane) {
  if (const std::optional<float> t = wlw::Raycast(ray, plane)) return ray.At(*t);
  return std::nullopt;
}
WLW_DEPRECATED(0.3, "Use wlw::TransformBox")
inline wlw::AABB TransformBox(const glm::mat4& transform, const wlw::AABB& box) {
  return wlw::TransformBox(transform, box);
}
WLW_DEPRECATED(0.3, "Use wlw::Union") inline wlw::AABB Union(const wlw::AABB& a, const wlw::AABB& b) {
  return wlw::Union(a, b);
}

}  // namespace wlw::scene
#endif
