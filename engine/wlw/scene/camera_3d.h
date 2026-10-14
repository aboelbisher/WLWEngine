#pragma once

#include <memory>
#include <array>
#include "wlw/core/vector3.h"
#include "wlw/core/vector2.h"
#include "wlw/core/collision.h"
#include <glm/gtc/matrix_transform.hpp>

namespace wlw::scene {

struct Frustum {
  std::array<Plane, 6> planes;  // Left, Right, Bottom, Top, Near, Far

  static Frustum FromMatrix(const glm::mat4& matrix) {
    Frustum f;
    // Left
    f.planes[0].normal = {matrix[0][3] + matrix[0][0], matrix[1][3] + matrix[1][0], matrix[2][3] + matrix[2][0]};
    f.planes[0].distance = matrix[3][3] + matrix[3][0];
    // Right
    f.planes[1].normal = {matrix[0][3] - matrix[0][0], matrix[1][3] - matrix[1][0], matrix[2][3] - matrix[2][0]};
    f.planes[1].distance = matrix[3][3] - matrix[3][0];
    // Bottom
    f.planes[2].normal = {matrix[0][3] + matrix[0][1], matrix[1][3] + matrix[1][1], matrix[2][3] + matrix[2][1]};
    f.planes[2].distance = matrix[3][3] + matrix[3][1];
    // Top
    f.planes[3].normal = {matrix[0][3] - matrix[0][1], matrix[1][3] - matrix[1][1], matrix[2][3] - matrix[2][1]};
    f.planes[3].distance = matrix[3][3] - matrix[3][1];
    // Near
    f.planes[4].normal = {matrix[0][3] + matrix[0][2], matrix[1][3] + matrix[1][2], matrix[2][3] + matrix[2][2]};
    f.planes[4].distance = matrix[3][3] + matrix[3][2];
    // Far
    f.planes[5].normal = {matrix[0][3] - matrix[0][2], matrix[1][3] - matrix[1][2], matrix[2][3] - matrix[2][2]};
    f.planes[5].distance = matrix[3][3] - matrix[3][2];

    for (auto& p : f.planes) {
      float length = glm::length(p.normal);
      p.normal /= length;
      p.distance /= length;
    }
    return f;
  }

  bool TestAABB(const scene::AABB& aabb) const {
    for (const auto& plane : planes) {
      glm::vec3 positive_vertex = {plane.normal.x >= 0 ? aabb.max.x : aabb.min.x,
                                   plane.normal.y >= 0 ? aabb.max.y : aabb.min.y,
                                   plane.normal.z >= 0 ? aabb.max.z : aabb.min.z};

      if (plane.GetDistanceToPoint(positive_vertex) < 0) {
        return false;  // Fully outside this plane
      }
    }
    return true;
  }
};

enum class CameraMovement {
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT,
  UP,
  DOWN,
  RIGHT_ROTATE,
  LEFT_ROTATE,
  UP_ROTATE,
  DOWN_ROTATE
};

class Camera3D {
 public:
  virtual ~Camera3D() = default;

  virtual void SetPosition(const core::Vector3& position) = 0;
  virtual void SetTarget(const core::Vector3& target) = 0;
  // virtual void SetCameraUp(const core::Vector3& up) = 0;
  virtual void UpdateSize(const core::Vector2& size) = 0;

  virtual const core::Vector3& GetPosition() const = 0;
  virtual const glm::mat4& GetViewMatrix() = 0;
  virtual const glm::mat4& GetProjectionMatrix() = 0;

  virtual void ProcessKeyboard(CameraMovement direction, float deltaTime) = 0;
  virtual void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) = 0;

  // The world-space ray "under" a screen point: every world point on it is drawn at that
  // point. Intersect it with something (IntersectPlane, an AABB...) to find what the
  // finger/cursor is over. `screen_size` must be in the same units as `screen_point`
  // (use input::Input::PointerPosition() and SurfaceSize()). Works for any projection:
  // perspective rays fan out from the eye, orthographic rays are all parallel.
  Ray ScreenPointToRay(const core::Vector2& screen_point, const core::Vector2& screen_size) {
    // 1. Screen -> NDC. Screen y grows downwards, NDC y grows upwards: flip it.
    const float ndc_x = 2.0f * screen_point.x / screen_size.x - 1.0f;
    const float ndc_y = 1.0f - 2.0f * screen_point.y / screen_size.y;

    // 2. NDC -> world: undo projection * view. A screen point has no depth, so unproject
    //    it twice, on the near plane (NDC z = -1) and the far plane (z = +1). The GL
    //    convention is z in [-1, 1]; a Vulkan backend would use [0, 1] here.
    const glm::mat4 inverse_view_projection = glm::inverse(GetProjectionMatrix() * GetViewMatrix());
    glm::vec4 near_point = inverse_view_projection * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
    glm::vec4 far_point = inverse_view_projection * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
    // Undo the perspective divide (w is 1 for orthographic, so this is a no-op there).
    near_point /= near_point.w;
    far_point /= far_point.w;

    // 3. The ray runs from the near point through the far point.
    return {glm::vec3(near_point), glm::normalize(glm::vec3(far_point - near_point))};
  }

  // The other direction: where a world point is drawn on screen (what the GPU does to every
  // vertex). x, y in the units of `screen_size`, origin top-left; z = depth in NDC (-1 near,
  // +1 far; outside that range the point is clipped). For UI that follows 3D objects.
  glm::vec3 WorldToScreen(const glm::vec3& world, const core::Vector2& screen_size) {
    const glm::vec4 clip = GetProjectionMatrix() * GetViewMatrix() * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;  // the perspective divide
    return {(ndc.x + 1.0f) * 0.5f * screen_size.x, (1.0f - ndc.y) * 0.5f * screen_size.y, ndc.z};
  }
};
}  // namespace wlw::scene