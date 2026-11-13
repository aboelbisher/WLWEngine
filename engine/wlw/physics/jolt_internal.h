#pragma once

// wlw::physics's own header for its .cpp files: Jolt's headers, conversions between glm and Jolt, the one-time Jolt
// setup. Never included by public headers (games and the engine don't see Jolt).

// Jolt.h first, always (it sets up the macros and memory functions its other headers use).
#include <Jolt/Jolt.h>
// clang-format off
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
// clang-format on

#include "wlw/core/log.h"
#include "wlw/core/math.h"
#include "wlw/physics/shape.h"

namespace wlw::physics {

WLW_LOG_CATEGORY(LogPhysics);  // WLW_LOG(LogPhysics, Warning, ...)

struct Shape::Impl {
  JPH::RefConst<JPH::Shape> shape;
};
using JoltShape = Shape::InternalAccess::Impl;

namespace detail {

// Registers Jolt's allocator, trace / assert hooks (into the engine's log and asserts) and its types, once per process
// (thread-safe). Every entry point that touches Jolt calls it first (shapes can be made before any World).
void EnsureJolt();

inline JPH::Vec3 ToJolt(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat ToJolt(const Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline Vec3 ToWlw(JPH::Vec3Arg v) { return Vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline Quat ToWlw(JPH::QuatArg q) { return Quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

}  // namespace detail
}  // namespace wlw::physics
