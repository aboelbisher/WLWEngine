#pragma once

#include <vector>
#include <concepts>
#include <type_traits>

#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "core/mesh.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp> // For passing to OpenGL

namespace wlw::scene {

template <core::OnlyVerticesTypes T>
class Node {
public:
	const core::Mesh<T>& GetMesh() const {
		return mesh_;
	}

	void SetMesh(const core::Mesh<T>& mesh) {
		mesh_ = mesh;
	}

	void SetPosition(const core::Vector3& position) {
		position_ = position;
		UpdateModelMatrix();
	}

	void SetScale(const core::Vector3& scale) {
		scale_ = scale;
		UpdateModelMatrix();
	}

	void SetRotation(const core::Vector3& rotation_angle_degrees) {
		// Store rotation angles (in radians)
		rotation_angles_degrees_ = rotation_angle_degrees;
		UpdateModelMatrix();
	}


	void UpdateModelMatrix() {
		// Convert degrees to radians for GLM
		glm::vec3 rotation_angle_radians_ = glm::radians(glm::vec3(
			rotation_angles_degrees_.x,
			rotation_angles_degrees_.y,
			rotation_angles_degrees_.z
		));
		// Start with Identity Matrix
		model_matrix_ = glm::mat4(1.0f);
		// Apply SCALE
		model_matrix_ = glm::scale(model_matrix_, glm::vec3(scale_.x, scale_.y, scale_.z));
		// Apply ROTATION (Y-axis example, add others as needed)
		model_matrix_ = glm::rotate(model_matrix_, rotation_angle_radians_.x, glm::vec3(1.0f, 0.0f, 0.0f));
		model_matrix_ = glm::rotate(model_matrix_, rotation_angle_radians_.y, glm::vec3(0.0f, 1.0f, 0.0f));
		model_matrix_ = glm::rotate(model_matrix_, rotation_angle_radians_.z, glm::vec3(0.0f, 0.0f, 1.0f));
		// Add other axes as needed (e.g., rotationAnglesRadians.x for pitch, rotationAnglesRadians.z for roll)
		// Apply TRANSLATION
		model_matrix_ = glm::translate(model_matrix_, glm::vec3(position_.x, position_.y, position_.z));
	}

	const glm::mat4& GetModelMatrix() const {
		return model_matrix_;
	}


private:
	core::Mesh<T> mesh_;

	core::Vector3 position_ = { 0,0,0 };
	core::Vector3 scale_ = {1,1,1};
	core::Vector3 rotation_angles_degrees_ = { 0,0,0 };

	glm::mat4 model_matrix_ = glm::mat4(1.0f);
};

using Node3D = Node<core::Vertex3D>;
using Node2D = Node<core::Vertex2D>;

} // namespace wlw::scene