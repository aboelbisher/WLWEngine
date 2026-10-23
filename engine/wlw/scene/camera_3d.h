#pragma once

#include <cstdint>
#include <memory>

#include <glm/gtc/matrix_transform.hpp>

#include "wlw/core/geometry.h"
#include "wlw/core/math.h"

namespace wlw::scene {

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

  virtual void SetPosition(const Vec3& position) = 0;
  virtual void SetTarget(const Vec3& target) = 0;
  // virtual void SetCameraUp(const Vec3& up) = 0;
  virtual void UpdateSize(const Vec2& size) = 0;

  virtual const Vec3& GetPosition() const = 0;
  virtual const glm::mat4& GetViewMatrix() = 0;
  virtual const glm::mat4& GetProjectionMatrix() = 0;

  virtual void ProcessKeyboard(CameraMovement direction, float deltaTime) = 0;
  virtual void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) = 0;

  // The render layers this camera draws (Node3D::SetLayers): a node is drawn when it shares a bit with the mask.
  // Default: every layer. A minimap camera can leave out the player's effects, a UI-model camera see only its own.
  void SetCullMask(uint32_t mask) { cull_mask_ = mask; }
  uint32_t GetCullMask() const { return cull_mask_; }

  // The world-space ray "under" a screen point: every world point on it is drawn at that
  // point. Raycast it against something (a Plane, an AABB...) to find what the
  // finger/cursor is over. `screen_size` must be in the same units as `screen_point`
  // (use input::Input::PointerPosition() and SurfaceSize()). Works for any projection:
  // perspective rays fan out from the eye, orthographic rays are all parallel.
  wlw::Ray ScreenPointToRay(const Vec2& screen_point, const Vec2& screen_size) {
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
  glm::vec3 WorldToScreen(const glm::vec3& world, const Vec2& screen_size) {
    const glm::vec4 clip = GetProjectionMatrix() * GetViewMatrix() * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;  // the perspective divide
    return {(ndc.x + 1.0f) * 0.5f * screen_size.x, (1.0f - ndc.y) * 0.5f * screen_size.y, ndc.z};
  }

 private:
  uint32_t cull_mask_ = 0xFFFFFFFFu;
};
}  // namespace wlw::scene