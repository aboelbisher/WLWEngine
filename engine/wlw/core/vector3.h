#pragma once

// Deprecated in 0.3: use wlw::Vec3 (wlw/core/math.h, = glm::vec3) and glm's functions (glm::cross,
// glm::normalize, glm::rotate). Converts to and from it, so code written against the 0.2 API keeps compiling
// until it moves.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "wlw/core/deprecated.h"

#if !WLW_NO_DEPRECATED
namespace wlw::core {

struct WLW_DEPRECATED(0.3, "Use wlw::Vec3 (wlw/core/math.h)") Vector3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  constexpr Vector3() = default;
  constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  constexpr Vector3(const glm::vec3& v) : x(v.x), y(v.y), z(v.z) {}  // NOLINT: implicit on purpose (a shim)
  constexpr operator glm::vec3() const { return {x, y, z}; }         // NOLINT

  Vector3 operator+(const Vector3& other) const { return {x + other.x, y + other.y, z + other.z}; }
  Vector3 operator-(const Vector3& other) const { return {x - other.x, y - other.y, z - other.z}; }
  Vector3& operator+=(const Vector3& other) { return *this = *this + other; }
  Vector3& operator-=(const Vector3& other) { return *this = *this - other; }
  Vector3 operator-() const { return {-x, -y, -z}; }
  Vector3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
  Vector3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }

  Vector3 Cross(const Vector3& a) const { return glm::cross(glm::vec3(*this), glm::vec3(a)); }
  Vector3 Normalize() const { return glm::normalize(glm::vec3(*this)); }
  Vector3 Rotate(float angle_degrees, const Vector3& axis) const {
    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(angle_degrees), glm::vec3(axis));
    return glm::vec3(rotation * glm::vec4(x, y, z, 1.0f));
  }
};

}  // namespace wlw::core
#endif
