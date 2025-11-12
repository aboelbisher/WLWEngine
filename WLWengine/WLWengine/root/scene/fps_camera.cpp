#include "scene/fps_camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace wlw::scene {

  class FPSCameraImpl : public FPSCamera {
  public:
    FPSCameraImpl()
      : position_(0.56f, 12.16f, 2.2f), 
      yaw_(-90.0f), pitch_(0.0f), fov_(45.0f), near_plane_(0.1f), far_plane_(100.0f),
      movement_speed_(5.0f) { // 5 units per second 

      updateCameraVectors();

    }

    void SetPosition(const core::Vector3& position) override {
			position_ = { position.x, position.y, position.z };
      updateCameraVectors();
    }
    void SetTarget(const core::Vector3& target) override {
      // In an FPS camera, the target is calculated from Position + Front.
      // For simplicity, we just ignore this setter here and rely on rotation for direction.
    }
    //void SetCameraUp(const core::Vector3& up) override {
    //  world_up_ = {up.x, up.y, up.z};
    //  updateCameraVectors();
    //}
    void UpdateSize(const core::Vector2& size) override {
      aspect_ratio_ = size.x / size.y;
      projection_matrix_ = glm::perspective(glm::radians(fov_), aspect_ratio_, near_plane_, far_plane_);
    }

    const core::Vector3& GetPosition() const override { return position_; }
    const glm::mat4& GetViewMatrix() override {
      updateCameraVectors(); // Ensure the view matrix is always up-to-date before access
      return view_matrix_;
    }
    const glm::mat4& GetProjectionMatrix() override { return projection_matrix_; }

    // --- Core Movement Logic ---
    void ProcessKeyboard(CameraMovement direction, float deltaTime) override {
      float velocity = movement_speed_ * deltaTime;
      core::Vector3 movementVector;

      switch (direction) {
      case CameraMovement::FORWARD:
      case CameraMovement::BACKWARD: {
        // Project the Front vector onto the horizontal plane (XY)
        core::Vector3 horizontalFront = front_;
        horizontalFront.z = 0.0f; // <--- The key to staying grounded
        horizontalFront = horizontalFront.Normalize();

        if (direction == CameraMovement::FORWARD) {
          movementVector = horizontalFront;
        }
        else {
          movementVector = -horizontalFront;
        }
        break;
      }

      case CameraMovement::LEFT:
        movementVector = -right_;
        break;
      case CameraMovement::RIGHT:
        movementVector = right_;
        break;

        // UP/DOWN movement is now purely vertical 'flying' (along Z-axis)
      case CameraMovement::UP:
        movementVector = world_up_;
        break;
      case CameraMovement::DOWN:
        movementVector = -world_up_;
        break;

        // Rotations are processed and immediately return
      case CameraMovement::UP_ROTATE:
        pitch_ += 30.0f * deltaTime;
        if (pitch_ > 89.0f) pitch_ = 89.0f;
        updateCameraVectors();
        return;

      case CameraMovement::DOWN_ROTATE:
        pitch_ -= 30.0f * deltaTime;
        if (pitch_ < -89.0f) pitch_ = -89.0f;
        updateCameraVectors();
        return;

      case CameraMovement::LEFT_ROTATE:
        yaw_ -= 30.0f * deltaTime;
        updateCameraVectors();
        return;

      case CameraMovement::RIGHT_ROTATE:
        yaw_ += 30.0f * deltaTime; // Assuming 30.0f to match LEFT_ROTATE
        updateCameraVectors();
        return;
      }

      // Apply movement
      position_ += movementVector * velocity;
    }

    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch) override {
      // 1. Apply Sensitivity
      // This scales the raw input to a comfortable rate of turning.
      float sensitivity = 0.01f;
      xoffset *= sensitivity;
      yoffset *= sensitivity;

      // 2. Update Yaw and Pitch
      // Yaw: Adding X-offset turns the camera horizontally.
      yaw_ -= xoffset;

      // Pitch: Subtracting Y-offset moves the camera up/down. 
      // We subtract because screens typically have Y-coordinates increasing downwards.
      pitch_ += yoffset;

      // 3. Constrain Pitch (Look Up/Down)
      // Prevents the camera from flipping upside down.
      if (constrainPitch) {
        if (pitch_ > 89.0f) {
          pitch_ = 89.0f;
        }
        if (pitch_ < -89.0f) {
          pitch_ = -89.0f;
        }
      }

      // 4. Update Vectors
      // The core of the functionality: recalculate front/right/up based on the new angles.
      updateCameraVectors();
    }

		static constexpr core::Vector3 world_up_ = { 0.0f, 0.0f, 1.0f }; // Z-up coordinate system

  private:
    void updateCameraVectors() {
      // Calculate the new Front vector
      core::Vector3 front;
      // for- world up
      front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
      front.y = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
      front.z = sin(glm::radians(pitch_));

      // For this simple example, we assume Yaw/Pitch is fixed (looking towards -Z initially)
      // We will only use the Right and WorldUp vectors for simple strafing and elevation.

       front_ = front.Normalize();
       right_ = front.Cross(world_up_).Normalize();
			 up_ = right_.Cross(front_).Normalize();

      // Calculate view matrix
      view_matrix_ = glm::lookAt(glm::vec3(position_), glm::vec3(position_ + front),  glm::vec3(up_));
    }



  private:
    // Camera attributes
    core::Vector3 position_;
    core::Vector3 front_;   // The direction the camera is looking
    core::Vector3 right_;   // The vector pointing to the right
    core::Vector3 up_;      // The local up vector

    // Eular angles (used for rotation, but we focus on position here)
    float yaw_;
    float pitch_;

    // Projection attributes
    float aspect_ratio_;
    float fov_;
    float near_plane_;
    float far_plane_;

    // Matrices
    glm::mat4 view_matrix_;
    glm::mat4 projection_matrix_;

    // Movement speed
    float movement_speed_;

  };
  
  std::unique_ptr<FPSCamera> FPSCamera::Create() {
    return std::make_unique<FPSCameraImpl>();
	}
} // wlw::scene