#pragma once

#include <memory>

#include "wlw/core/deprecated.h"
#include "wlw/core/math.h"
#include "wlw/scene/camera_3d.h"

#if !WLW_NO_DEPRECATED
namespace wlw::scene {

WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
// 0.3's orthographic camera, kept for one release: a Camera3D made orthographic with the Contain fit, aimed at a
// target point (re-aimed whenever the position, target or up changes).
class WLW_DEPRECATED(0.3,
                     "Use Camera3D: SetOrthographic(height), SetFrameAspect(width / height), "
                     "SetFitMode(Contain), SetPosition + LookAt(target, up)") OrthographicCamera : public Camera3D {
 public:
  static std::unique_ptr<OrthographicCamera> Create() { return std::make_unique<OrthographicCamera>(); }

  OrthographicCamera() {
    SetOrthographic(10.0f, 0.1f, 100.0f);
    SetFitMode(FitMode::Contain);
    Node3D::SetPosition({0.0f, 0.0f, 10.0f});
    Aim();
  }

  void SetPosition(const Vec3& position) {
    Node3D::SetPosition(position);
    Aim();
  }
  void SetTarget(const Vec3& target) {
    target_ = target;
    Aim();
  }
  void SetUp(const Vec3& up) {
    up_ = up;
    Aim();
  }
  // The world-space area (width x height) that must stay visible.
  void SetVisibleArea(float width, float height) {
    SetSize(height);
    SetFrameAspect(height > 0.0f ? width / height : 1.0f);
  }

 private:
  void Aim() { LookAt(target_, up_); }

  Vec3 target_{0.0f};
  Vec3 up_{0.0f, 1.0f, 0.0f};
};
WLW_DISABLE_DEPRECATION_WARNINGS_END

}  // namespace wlw::scene
#endif
