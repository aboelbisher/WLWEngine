#include "wlw/physics/shape.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "wlw/core/assert.h"
#include "wlw/physics/jolt_internal.h"

// clang-format off
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

namespace wlw::physics {
namespace detail {
namespace {

void JoltTrace(const char* format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  WLW_LOG(LogPhysics, Info, "Jolt: %s", text);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line) {
  // The engine's assert: a Fatal log + stop in debug builds, or the test's handler.
  wlw::detail::AssertFailed(expression, file, static_cast<int>(line), " Jolt: %s", message ? message : "");
  return false;  // the engine's handler already broke into a debugger if one is attached
}
#endif

}  // namespace

void EnsureJolt() {
  static std::once_flag once;
  std::call_once(once, [] {
    // Process-wide and never undone: shapes may outlive every World, and Jolt's tables are small.
    JPH::RegisterDefaultAllocator();
    JPH::Trace = JoltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  });
}

}  // namespace detail

namespace {

Shape MakeShape(const JPH::ShapeSettings& settings, const char* what) {
  JPH::ShapeSettings::ShapeResult result = settings.Create();
  if (!WLW_VERIFY(result.IsValid(), "physics: can't make a %s shape: %s", what, result.GetError().c_str())) {
    return {};
  }
  auto impl = std::make_shared<JoltShape>();
  impl->shape = result.Get();
  return Shape::InternalAccess::Make(std::move(impl));
}

}  // namespace

Shape Shape::Box(Vec3 half_extents, float convex_radius) {
  detail::EnsureJolt();
  if (!WLW_VERIFY(half_extents.x > 0.0f && half_extents.y > 0.0f && half_extents.z > 0.0f,
                  "physics: box half extents must be > 0 (%g, %g, %g)", half_extents.x, half_extents.y,
                  half_extents.z)) {
    return {};
  }
  const float smallest = std::min({half_extents.x, half_extents.y, half_extents.z});
  JPH::BoxShapeSettings settings(detail::ToJolt(half_extents), std::clamp(convex_radius, 0.0f, smallest));
  return MakeShape(settings, "box");
}

Shape Shape::Sphere(float radius) {
  detail::EnsureJolt();
  if (!WLW_VERIFY(radius > 0.0f, "physics: sphere radius must be > 0 (%g)", radius)) {
    return {};
  }
  JPH::SphereShapeSettings settings(radius);
  return MakeShape(settings, "sphere");
}

Shape Shape::ConvexHull(std::span<const Vec3> points, float convex_radius) {
  detail::EnsureJolt();
  if (!WLW_VERIFY(points.size() >= 4, "physics: a convex hull needs at least 4 points (%zu)", points.size())) {
    return {};
  }
  JPH::Array<JPH::Vec3> jolt_points;
  jolt_points.reserve(points.size());
  for (const Vec3& point : points) {
    jolt_points.push_back(detail::ToJolt(point));
  }
  // Jolt shrinks the radius itself when the hull is too thin for it.
  JPH::ConvexHullShapeSettings settings(jolt_points, std::max(convex_radius, 0.0f));
  return MakeShape(settings, "convex hull");
}

AABB Shape::LocalBounds() const {
  if (!impl_) {
    return {};
  }
  const JPH::AABox box = impl_->shape->GetLocalBounds();
  return {detail::ToWlw(box.mMin), detail::ToWlw(box.mMax)};
}

float Shape::Volume() const { return impl_ ? impl_->shape->GetVolume() : 0.0f; }

Vec3 Shape::CenterOfMass() const { return impl_ ? detail::ToWlw(impl_->shape->GetCenterOfMass()) : Vec3(0.0f); }

}  // namespace wlw::physics
