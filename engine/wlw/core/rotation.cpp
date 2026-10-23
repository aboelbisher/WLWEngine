#include "wlw/core/rotation.h"

#include <cmath>

#include <glm/gtx/euler_angles.hpp>

namespace wlw {
namespace {

Quat AxisTurn(int axis, float radians) {
  Vec3 unit{0.0f};
  unit[axis] = 1.0f;
  return glm::angleAxis(radians, unit);
}

// The three axes of each order, left to right in the product.
struct Axes {
  int a, b, c;
};
Axes OrderAxes(EulerOrder order) {
  switch (order) {
    case EulerOrder::XYZ: return {0, 1, 2};
    case EulerOrder::XZY: return {0, 2, 1};
    case EulerOrder::YXZ: return {1, 0, 2};
    case EulerOrder::YZX: return {1, 2, 0};
    case EulerOrder::ZXY: return {2, 0, 1};
    case EulerOrder::ZYX: return {2, 1, 0};
  }
  return {1, 0, 2};
}

}  // namespace

Quat QuatFromEuler(const Vec3& radians, EulerOrder order) {
  const Axes axes = OrderAxes(order);
  return AxisTurn(axes.a, radians[axes.a]) * AxisTurn(axes.b, radians[axes.b]) * AxisTurn(axes.c, radians[axes.c]);
}

Vec3 EulerFromQuat(const Quat& rotation, EulerOrder order) {
  const Mat4 m = glm::mat4_cast(glm::normalize(rotation));
  float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f;  // the angles in product order
  switch (order) {
    case EulerOrder::XYZ: glm::extractEulerAngleXYZ(m, t1, t2, t3); break;
    case EulerOrder::XZY: glm::extractEulerAngleXZY(m, t1, t2, t3); break;
    case EulerOrder::YXZ: glm::extractEulerAngleYXZ(m, t1, t2, t3); break;
    case EulerOrder::YZX: glm::extractEulerAngleYZX(m, t1, t2, t3); break;
    case EulerOrder::ZXY: glm::extractEulerAngleZXY(m, t1, t2, t3); break;
    case EulerOrder::ZYX: glm::extractEulerAngleZYX(m, t1, t2, t3); break;
  }
  const Axes axes = OrderAxes(order);
  Vec3 out{0.0f};
  out[axes.a] = t1;
  out[axes.b] = t2;
  out[axes.c] = t3;
  return out;
}

Quat LookRotation(const Vec3& forward, const Vec3& up) {
  const float length_squared = glm::dot(forward, forward);
  if (length_squared < 1e-12f) return Quat(1.0f, 0.0f, 0.0f, 0.0f);
  const Vec3 f = forward / std::sqrt(length_squared);
  Vec3 u = SafeNormalize(up, kUp);
  if (glm::length(glm::cross(f, u)) < 1e-4f) {
    // Parallel: the other up keeps the sign, so straight down (0, 0, -1) with up +Z turns +Z to +Y (a pitch of
    // -90 degrees: the top of the picture is then forward).
    const float sign = glm::dot(f, u) > 0.0f ? 1.0f : -1.0f;
    u = (std::abs(u.y) < 0.9f ? kBack : kUp) * sign;
  }
  // Columns: right, forward, up, a right-handed basis (X x Y = Z).
  const Vec3 right = glm::normalize(glm::cross(f, u));
  const Vec3 true_up = glm::cross(right, f);
  return glm::normalize(glm::quat_cast(Mat3(right, f, true_up)));
}

}  // namespace wlw
