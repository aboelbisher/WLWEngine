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
      offset_ = {0.0f, 10.0f, 5.0f}; // Default to a more "above" view
      projection_ = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    }

    void SetOffset(const core::Vector3& offset) {
      offset_ = offset;
    }

    void SetPosition(const core::Vector3& position) override { position_ = position; }
    void SetTarget(const core::Vector3& target) override { target_pos_ = target; }
    void UpdateSize(const core::Vector2& size) override {
      projection_ = glm::perspective(glm::radians(45.0f), size.x / size.y, 0.1f, 100.0f);
    }

    const core::Vector3& GetPosition() const override { return position_; }

    const glm::mat4& GetViewMatrix() override {
      if (target_) {
        core::Vector3 t = target_->GetPosition();
        position_ = {t.x + offset_.x, t.y + offset_.y, t.z + offset_.z};
        view_ = glm::lookAt(
          glm::vec3(position_.x, position_.y, position_.z),
          glm::vec3(t.x, t.y, t.z),
          glm::vec3(0.0f, 1.0f, 0.0f)
        );
      }
      return view_;
    }

    const glm::mat4& GetProjectionMatrix() override { return projection_; }

    void ProcessKeyboard(CameraMovement direction, float deltaTime) override {}
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) override {}

  private:
    std::shared_ptr<Node3D> target_;
    core::Vector3 position_;
    core::Vector3 offset_;
    core::Vector3 target_pos_;
    glm::mat4 view_;
    glm::mat4 projection_;
  };
}
