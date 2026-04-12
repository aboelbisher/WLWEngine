#pragma once

#include <memory>
#include <array>
#include "core/vector3.h"
#include "core/vector2.h"
#include "core/collision.h"
#include <glm/gtc/matrix_transform.hpp>

namespace wlw::scene {

    struct Plane {
        glm::vec3 normal;
        float distance;

        float GetDistanceToPoint(const glm::vec3& point) const {
            return glm::dot(normal, point) + distance;
        }
    };

    struct Frustum {
        std::array<Plane, 6> planes; // Left, Right, Bottom, Top, Near, Far

        static Frustum FromMatrix(const glm::mat4& matrix) {
            Frustum f;
            // Left
            f.planes[0].normal = { matrix[0][3] + matrix[0][0], matrix[1][3] + matrix[1][0], matrix[2][3] + matrix[2][0] };
            f.planes[0].distance = matrix[3][3] + matrix[3][0];
            // Right
            f.planes[1].normal = { matrix[0][3] - matrix[0][0], matrix[1][3] - matrix[1][0], matrix[2][3] - matrix[2][0] };
            f.planes[1].distance = matrix[3][3] - matrix[3][0];
            // Bottom
            f.planes[2].normal = { matrix[0][3] + matrix[0][1], matrix[1][3] + matrix[1][1], matrix[2][3] + matrix[2][1] };
            f.planes[2].distance = matrix[3][3] + matrix[3][1];
            // Top
            f.planes[3].normal = { matrix[0][3] - matrix[0][1], matrix[1][3] - matrix[1][1], matrix[2][3] - matrix[2][1] };
            f.planes[3].distance = matrix[3][3] - matrix[3][1];
            // Near
            f.planes[4].normal = { matrix[0][3] + matrix[0][2], matrix[1][3] + matrix[1][2], matrix[2][3] + matrix[2][2] };
            f.planes[4].distance = matrix[3][3] + matrix[3][2];
            // Far
            f.planes[5].normal = { matrix[0][3] - matrix[0][2], matrix[1][3] - matrix[1][2], matrix[2][3] - matrix[2][2] };
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
                glm::vec3 positive_vertex = {
                    plane.normal.x >= 0 ? aabb.max.x : aabb.min.x,
                    plane.normal.y >= 0 ? aabb.max.y : aabb.min.y,
                    plane.normal.z >= 0 ? aabb.max.z : aabb.min.z
                };

                if (plane.GetDistanceToPoint(positive_vertex) < 0) {
                    return false; // Fully outside this plane
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
		//virtual void SetCameraUp(const core::Vector3& up) = 0;
		virtual void UpdateSize(const core::Vector2& size) = 0;

		virtual const core::Vector3& GetPosition() const = 0;
		virtual const glm::mat4& GetViewMatrix() = 0;
		virtual const glm::mat4& GetProjectionMatrix() = 0;

		virtual void ProcessKeyboard(CameraMovement direction, float deltaTime) = 0;
		virtual void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) = 0;

	};
} // namespace wlw::scene