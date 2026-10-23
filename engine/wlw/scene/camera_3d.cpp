#include "wlw/scene/camera_3d.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace wlw::scene {
namespace {

float ValidAspect(float aspect) { return aspect > 0.0f && std::isfinite(aspect) ? aspect : 1.0f; }

}  // namespace

float Camera3D::HalfHeight(float aspect) const {
  const float frame_half_height = IsOrthographic() ? size_ * 0.5f : std::tan(fov_y_ * 0.5f);
  const float frame_half_width = frame_half_height * frame_aspect_;
  switch (fit_mode_) {
    case FitMode::KeepHeight: return frame_half_height;
    case FitMode::KeepWidth: return frame_half_width / aspect;
    case FitMode::Contain: return std::max(frame_half_height, frame_half_width / aspect);
    case FitMode::Cover: return std::min(frame_half_height, frame_half_width / aspect);
  }
  return frame_half_height;
}

Mat4 Camera3D::GetViewMatrix() const {
  const Mat4& world = GetWorldMatrix();
  // The world transform's rotation and position, inverted: R^T and -R^T * p. R = its axes orthonormalized
  // (Gram-Schmidt from the back axis, so the camera looks exactly along Forward(); then up, then right): scale
  // is dropped, and so is the shear a non-uniformly scaled parent puts on a rotated child.
  const Vec3 z = SafeNormalize(Vec3(world[2]), Vec3(0.0f, 0.0f, 1.0f));
  const Vec3 up = Vec3(world[1]);
  Vec3 y = SafeNormalize(up - glm::dot(up, z) * z, Vec3(0.0f));
  if (y == Vec3(0.0f)) y = SafeNormalize(glm::cross(z, Vec3(world[0])), Vec3(0.0f, 1.0f, 0.0f));  // up along z
  const Vec3 x = glm::cross(y, z);
  const Vec3 p(world[3]);
  Mat4 view(1.0f);
  view[0] = Vec4(x.x, y.x, z.x, 0.0f);
  view[1] = Vec4(x.y, y.y, z.y, 0.0f);
  view[2] = Vec4(x.z, y.z, z.z, 0.0f);
  view[3] = Vec4(-glm::dot(x, p), -glm::dot(y, p), -glm::dot(z, p), 1.0f);
  return view;
}

Mat4 Camera3D::GetProjectionMatrix(float aspect) const {
  aspect = ValidAspect(aspect);
  const float half_height = HalfHeight(aspect);
  if (IsOrthographic()) {
    const float half_width = half_height * aspect;
    return glm::ortho(-half_width, half_width, -half_height, half_height, near_, far_);
  }
  return glm::perspective(2.0f * std::atan(half_height), aspect, near_, far_);
}

Vec2 Camera3D::VisibleSize(float aspect, float distance) const {
  aspect = ValidAspect(aspect);
  const float height = 2.0f * HalfHeight(aspect) * (IsOrthographic() ? 1.0f : distance);
  return {height * aspect, height};
}

wlw::Ray Camera3D::ScreenPointToRay(const Vec2& screen_point, const Vec2& screen_size) const {
  // 1. Screen -> NDC. Screen y grows downwards, NDC y grows upwards: flip it.
  const float ndc_x = 2.0f * screen_point.x / screen_size.x - 1.0f;
  const float ndc_y = 1.0f - 2.0f * screen_point.y / screen_size.y;

  // 2. NDC -> world: undo projection * view. A screen point has no depth, so unproject it twice, on the near plane
  //    (NDC z = -1) and the far plane (z = +1). The GL convention is z in [-1, 1]; a Vulkan backend would use [0, 1].
  const Mat4 inverse_view_projection = glm::inverse(GetViewProjectionMatrix(screen_size.x / screen_size.y));
  Vec4 near_point = inverse_view_projection * Vec4(ndc_x, ndc_y, -1.0f, 1.0f);
  Vec4 far_point = inverse_view_projection * Vec4(ndc_x, ndc_y, 1.0f, 1.0f);
  // Undo the perspective divide (w is 1 for orthographic, so this is a no-op there).
  near_point /= near_point.w;
  far_point /= far_point.w;

  // 3. The ray runs from the near point through the far point.
  return {Vec3(near_point), glm::normalize(Vec3(far_point - near_point))};
}

Vec3 Camera3D::WorldToScreen(const Vec3& world, const Vec2& screen_size) const {
  const Vec4 clip = GetViewProjectionMatrix(screen_size.x / screen_size.y) * Vec4(world, 1.0f);
  const Vec3 ndc = Vec3(clip) / clip.w;  // the perspective divide
  return {(ndc.x + 1.0f) * 0.5f * screen_size.x, (1.0f - ndc.y) * 0.5f * screen_size.y, ndc.z};
}

}  // namespace wlw::scene
