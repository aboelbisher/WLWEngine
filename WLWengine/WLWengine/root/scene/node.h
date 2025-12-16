#pragma once

#include <vector>
#include <concepts>
#include <type_traits>
#include <unordered_map>

#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "core/model.h"
#include "core/logger.h"
#include "rendering/material.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp> // For passing to OpenGL

namespace wlw::scene {

template <core::OnlyVerticesTypes T>
class Node {
public:

	std::shared_ptr <core::Model<T>> GetModel() const {
		return model_;
	}

	void SetModel(std::shared_ptr <core::Model<T>> model) {
		model_ = model;
	}

	void Translate(const core::Vector3& vector) {
		position_ += vector;
		for (const auto& [_, child_] : children_) {
			child_->Translate(vector);
		}
		UpdateModelMatrix();
	}

	void SetPosition(const core::Vector3& position) {
		auto old_position = position_;
		position_ = position;
		UpdateModelMatrix();
		auto translation_vector = position - old_position;
		for (const auto& [_, child_] : children_) {
			child_->Translate(translation_vector);
		}
	}

	core::Vector3 GetPosition() const {
		return position_;
	}

	void SetScale(const core::Vector3& scale) {
		scale_ = scale;
		UpdateModelMatrix();
		for (const auto& [_, child_] : children_) {
			child_->SetScale(scale);
		}
	}

	void SetRotation(const core::Vector3& rotation_angle_degrees) {
		rotation_angles_degrees_ = rotation_angle_degrees;
		UpdateModelMatrix();

		for (const auto& [_, child_] : children_) {
			child_->SetRotation(rotation_angle_degrees);
		}
	}

	const glm::mat4& GetModelMatrix() const {
		return model_matrix_;
	}

	void SetMaterial(std::shared_ptr<rendering::Material> material) {
		material_ = material;

		//TODO: should this be done here ?
		
		if (material_->HasTexture()) {
			material_->BindTexture();
			material_->GenerateTexture();
		}
	}

	const std::shared_ptr<rendering::Material> GetMaterial() const {
		return material_;
	}

	int AddNode(std::shared_ptr<Node> node) {
		children_.insert({current_node_id_, node});
		return current_node_id_++;
	}

	const std::unordered_map<int, std::shared_ptr<scene::Node<T>>>& GetChildren() const  {
		return children_;
	}

private:

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

private:
	std::shared_ptr <core::Model<T>> model_;

	core::Vector3 position_ = { 0,0,0 };
	core::Vector3 scale_ = {1,1,1};
	core::Vector3 rotation_angles_degrees_ = { 0,0,0 };

	glm::mat4 model_matrix_ = glm::mat4(1.0f);

	std::shared_ptr<rendering::Material> material_;

	std::unordered_map<int, std::shared_ptr<scene::Node<T>>>  children_;
	int current_node_id_ = 0;
};

using Node3D = Node<core::Vertex3D>;
using Node2D = Node<core::Vertex2D>;

using Nodes2DMap = std::unordered_map<int, std::shared_ptr<Node2D>>;
using Nodes3DMap = std::unordered_map<int, std::shared_ptr<Node3D>>;


} // namespace wlw::scene