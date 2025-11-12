#pragma once

#include <memory>
#include "core/vector3.h"
#include "core/vector2.h"
#include <glm/gtc/matrix_transform.hpp>

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

		virtual void SetPosition(const core::Vector3& position) = 0;
		virtual void SetTarget(const core::Vector3& target) = 0;
		//virtual void SetCameraUp(const core::Vector3& up) = 0;
		virtual void UpdateSize(const core::Vector2& size) = 0;

		virtual const core::Vector3& GetPosition() const = 0;
		virtual const glm::mat4& GetViewMatrix() = 0;
		virtual const glm::mat4& GetProjectionMatrix() = 0;

		virtual void ProcessKeyboard(CameraMovement direction, float deltaTime) = 0;
		virtual void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) = 0;

	};
} // namespace wlw::scene