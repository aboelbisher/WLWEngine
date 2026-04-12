#pragma once

#include <vector>
#include <concepts>
#include <type_traits>
#include <unordered_map>

#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "core/model.h"
#include "core/logger.h"
#include "core/collision.h"
#include "rendering/material.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp> // For passing to OpenGL

namespace wlw::scene {

template <core::OnlyVerticesTypes T>
class Node {
public:

    scene::AABB GetAABB() const {
        if (!model_ || model_->meshes.empty()) {
            return scene::AABB::FromPositionAndSize(position_, 0.1f);
        }

        glm::vec3 minBound(std::numeric_limits<float>::max());
        glm::vec3 maxBound(std::numeric_limits<float>::lowest());

        for (const auto& mesh : model_->meshes) {
            const auto& local = mesh->GetLocalAABB();

            // The 8 corners of the local AABB
            std::array<glm::vec3, 8> corners = {
                glm::vec3{local.min.x, local.min.y, local.min.z},
                glm::vec3{local.min.x, local.min.y, local.max.z},
                glm::vec3{local.min.x, local.max.y, local.min.z},
                glm::vec3{local.min.x, local.max.y, local.max.z},
                glm::vec3{local.max.x, local.min.y, local.min.z},
                glm::vec3{local.max.x, local.min.y, local.max.z},
                glm::vec3{local.max.x, local.max.y, local.min.z},
                glm::vec3{local.max.x, local.max.y, local.max.z}
            };

            for (const auto& corner : corners) {
                glm::vec4 worldPos = model_matrix_ * glm::vec4(corner, 1.0f);
                minBound = glm::min(minBound, glm::vec3(worldPos));
                maxBound = glm::max(maxBound, glm::vec3(worldPos));
            }
        }
        return { {minBound.x, minBound.y, minBound.z}, {maxBound.x, maxBound.y, maxBound.z} };
    }


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

	const core::Vector3& GetScale() const {
		return scale_;
	}

	const core::Vector3& GetRotation() const {
		return rotation_angles_degrees_;
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
		
		// 1. Apply TRANSLATION (Translate to world position)
		model_matrix_ = glm::translate(model_matrix_, glm::vec3(position_.x, position_.y, position_.z));

		// 2. Apply ROTATION (Rotate around local origin)
		model_matrix_ = glm::rotate(model_matrix_, rotation_angle_radians_.x, glm::vec3(1.0f, 0.0f, 0.0f));
		model_matrix_ = glm::rotate(model_matrix_, rotation_angle_radians_.y, glm::vec3(0.0f, 1.0f, 0.0f));
		model_matrix_ = glm::rotate(model_matrix_, rotation_angle_radians_.z, glm::vec3(0.0f, 0.0f, 1.0f));

		// 3. Apply SCALE (Scale around local origin)
		model_matrix_ = glm::scale(model_matrix_, glm::vec3(scale_.x, scale_.y, scale_.z));
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