#pragma once

#include "camera_3d.h"
#include "node.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace wlw::scene {
class FollowCamera : public Camera3D {
 public:
  static std::unique_ptr<FollowCamera> Create(std::shared_ptr<Node3D> target) {
    return std::make_unique<FollowCamera>(target);
  }

  FollowCamera(std::shared_ptr<Node3D> target) : target_(target) {
    offset_ = {0.0f, 10.0f, 5.0f};  // Default to a more "above" view
    projection_ = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
  }

  void SetOffset(const Vec3& offset) { offset_ = offset; }

  void SetPosition(const Vec3& position) override { position_ = position; }
  void SetTarget(const Vec3& target) override { target_pos_ = target; }
  void UpdateSize(const Vec2& size) override {
    projection_ = glm::perspective(glm::radians(45.0f), size.x / size.y, 0.1f, 100.0f);
  }

  const Vec3& GetPosition() const override { return position_; }

  const glm::mat4& GetViewMatrix() override {
    if (target_) {
      Vec3 t = target_->GetPosition();
      position_ = t + offset_;
      view_ = glm::lookAt(position_, t, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    return view_;
  }

  const glm::mat4& GetProjectionMatrix() override { return projection_; }

  void ProcessKeyboard(CameraMovement direction, float deltaTime) override {}
  void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) override {}

 private:
  std::shared_ptr<Node3D> target_;
  Vec3 position_;
  Vec3 offset_;
  Vec3 target_pos_;
  glm::mat4 view_;
  glm::mat4 projection_;
};
}  // namespace wlw::scene
