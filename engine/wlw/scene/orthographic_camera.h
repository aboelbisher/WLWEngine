#pragma once

#include <algorithm>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "camera_3d.h"

namespace wlw::scene {

// A camera with an orthographic (parallel) projection: no perspective, so an object's size on
// screen doesn't depend on its distance from the camera. That's what you want for top-down /
// side-on 2D-style games, isometric views, UI and editor views.
//
// Instead of a field of view, the camera has a visible *area* in world units. You say "keep at
// least W x H world units visible" and the camera widens one side to match the screen's aspect
// ratio, so the area always fits fully on screen (extra space shows on the longer side).
class OrthographicCamera : public Camera3D {
 public:
  static std::shared_ptr<OrthographicCamera> Create() { return std::make_shared<OrthographicCamera>(); }

  OrthographicCamera() {
    RebuildView();
    RebuildProjection();
  }

  // ---- View: where the camera is and where it looks ----
  void SetPosition(const core::Vector3& position) override {
    position_ = position;
    RebuildView();
  }
  void SetTarget(const core::Vector3& target) override {
    target_ = target;
    RebuildView();
  }
  // Which world direction points "up" on screen. Must not be parallel to the viewing direction:
  // a camera looking straight down -y needs e.g. {0, 0, -1}, not {0, 1, 0}.
  void SetUp(const core::Vector3& up) {
    up_ = up;
    RebuildView();
  }

  // ---- Projection ----
  // The world-space area (width x height, centred on the view direction) that must stay visible.
  void SetVisibleArea(float width, float height) {
    visible_width_ = width;
    visible_height_ = height;
    RebuildProjection();
  }
  // Depth range along the viewing direction, measured from the camera position. In orthographic
  // projection depth is linear, so a large range costs much less precision than with perspective.
  void SetClipPlanes(float near_plane, float far_plane) {
    near_ = near_plane;
    far_ = far_plane;
    RebuildProjection();
  }
  void UpdateSize(const core::Vector2& size) override {
    if (size.x <= 0 || size.y <= 0) return;
    aspect_ = size.x / size.y;
    RebuildProjection();
  }

  // The half-extents actually shown after fitting the visible area to the screen's aspect ratio.
  float GetHalfWidth() const { return half_width_; }
  float GetHalfHeight() const { return half_height_; }

  const core::Vector3& GetPosition() const override { return position_; }
  const glm::mat4& GetViewMatrix() override { return view_; }
  const glm::mat4& GetProjectionMatrix() override { return projection_; }

  // No built-in fly controls: games move this camera explicitly.
  void ProcessKeyboard(CameraMovement, float) override {}
  void ProcessMouseMovement(float, float, bool) override {}

 private:
  void RebuildView() {
    view_ = glm::lookAt(glm::vec3(position_.x, position_.y, position_.z), glm::vec3(target_.x, target_.y, target_.z),
                        glm::vec3(up_.x, up_.y, up_.z));
  }

  void RebuildProjection() {
    // Fit: start from the height that's needed, then grow it if the width wouldn't fit.
    half_height_ = std::max(visible_height_ / 2.0f, (visible_width_ / 2.0f) / aspect_);
    half_width_ = half_height_ * aspect_;
    projection_ = glm::ortho(-half_width_, half_width_, -half_height_, half_height_, near_, far_);
  }

  core::Vector3 position_ = {0, 0, 10};
  core::Vector3 target_ = {0, 0, 0};
  core::Vector3 up_ = {0, 1, 0};

  float visible_width_ = 10.0f;
  float visible_height_ = 10.0f;
  float near_ = 0.1f;
  float far_ = 100.0f;
  float aspect_ = 1.0f;
  float half_width_ = 5.0f;
  float half_height_ = 5.0f;

  glm::mat4 view_{1.0f};
  glm::mat4 projection_{1.0f};
};

}  // namespace wlw::scene
