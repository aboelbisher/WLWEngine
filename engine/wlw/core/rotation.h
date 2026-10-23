#pragma once

// Rotations as quaternions, and the two ways people write them: Euler angles and "look at".
//
// Euler angles are three turns about the X, Y and Z axes, in radians, applied in an order. The order names the
// matrix product left to right: ZXY = R = Rz * Rx * Ry, so a point is turned about Y first, then X, then Z (the
// default for the engine's Z up, +Y forward: yaw about Z, then pitch about X, then roll about Y, as Godot's and
// Unity's YXZ is for theirs). The Vec3 always holds (x, y, z) whatever the order: (pitch, roll, yaw) for ZXY.
// Going back from a quaternion to angles is not unique (190° comes back as (180, -10, 180) in some orders);
// scene::Node3D keeps the angles it was given, so an inspector shows what was typed.
#include "wlw/core/math.h"

namespace wlw {

enum class EulerOrder { XYZ, XZY, YXZ, YZX, ZXY, ZYX };
inline constexpr EulerOrder kDefaultEulerOrder = EulerOrder::ZXY;  // yaw, pitch, roll

// The rotation of the angles `radians` (x, y, z) applied in `order`.
Quat QuatFromEuler(const Vec3& radians, EulerOrder order = kDefaultEulerOrder);

// Angles (x, y, z) in radians that give `rotation` in `order`. The middle angle is in [-pi/2, pi/2], the others
// in [-pi, pi]; at the middle angle's limits (gimbal lock) the split between the other two is arbitrary.
Vec3 EulerFromQuat(const Quat& rotation, EulerOrder order = kDefaultEulerOrder);

// The rotation that turns +Y (forward) to `forward` and keeps +Z as close to `up` as it can (like Unity's
// Quaternion.LookRotation, for the engine's axes). `forward` needn't be normalized; a zero one gives the
// identity. When `forward` is parallel to `up`, another up is used (-+Y, or +-Z when `up` is along Y), so the
// result is always a valid rotation: straight down with up +Z is a pitch of -90 degrees (+Z ends up along +Y).
Quat LookRotation(const Vec3& forward, const Vec3& up = kUp);

}  // namespace wlw
