#pragma once

#include <array>
#include <limits>
#include <memory>
#include <unordered_map>

#include "wlw/core/collision.h"
#include "wlw/core/logger.h"
#include "wlw/core/model.h"
#include "wlw/core/vertex_2d.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/rendering/material.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace wlw::scene {

// A scene-graph node. Position / rotation / scale are LOCAL (relative to the parent);
// GetModelMatrix() returns the WORLD matrix = parent's world matrix * local matrix.
template <core::OnlyVerticesTypes T>
class Node {
public:
	Node() = default;
	Node(const Node&) = delete;             // children hold a back-pointer to their parent,
	Node& operator=(const Node&) = delete;  // so copying would leave them pointing at the wrong node

	virtual ~Node() {
		for (const auto& [_, child] : children_) {
			if (child) {
				child->parent_ = nullptr;
				child->id_in_parent_ = -1;
			}
		}
	}

	// ---- Model / material ----
	std::shared_ptr<core::Model<T>> GetModel() const { return model_; }
	void SetModel(std::shared_ptr<core::Model<T>> model) { model_ = model; }

	void SetMaterial(std::shared_ptr<rendering::Material> material) { material_ = material; }
	const std::shared_ptr<rendering::Material> GetMaterial() const { return material_; }

	// ---- Local transform ----
	void SetPosition(const core::Vector3& position) {
		position_ = position;
		UpdateLocalMatrix();
	}
	core::Vector3 GetPosition() const { return position_; }

	void Translate(const core::Vector3& delta) {
		position_ += delta;
		UpdateLocalMatrix();
	}

	void SetScale(const core::Vector3& scale) {
		scale_ = scale;
		UpdateLocalMatrix();
	}
	const core::Vector3& GetScale() const { return scale_; }

	void SetRotation(const core::Vector3& rotation_angle_degrees) {
		rotation_angles_degrees_ = rotation_angle_degrees;
		UpdateLocalMatrix();
	}
	const core::Vector3& GetRotation() const { return rotation_angles_degrees_; }

	const glm::mat4& GetLocalMatrix() const { return local_matrix_; }

	// ---- World transform (includes all parents) ----
	glm::mat4 GetModelMatrix() const {
		return parent_ ? parent_->GetModelMatrix() * local_matrix_ : local_matrix_;
	}

	core::Vector3 GetWorldPosition() const {
		const glm::mat4 world = GetModelMatrix();
		return {world[3].x, world[3].y, world[3].z};
	}

	// World-space bounding box of this node's model (children not included).
	scene::AABB GetAABB() const {
		const glm::mat4 world = GetModelMatrix();
		if (!model_ || model_->meshes.empty()) {
			return scene::AABB::FromPositionAndSize({world[3].x, world[3].y, world[3].z}, 0.1f);
		}

		glm::vec3 min_bound(std::numeric_limits<float>::max());
		glm::vec3 max_bound(std::numeric_limits<float>::lowest());

		for (const auto& mesh : model_->meshes) {
			const auto& local = mesh->GetLocalAABB();
			const std::array<glm::vec3, 8> corners = {
				glm::vec3{local.min.x, local.min.y, local.min.z}, glm::vec3{local.min.x, local.min.y, local.max.z},
				glm::vec3{local.min.x, local.max.y, local.min.z}, glm::vec3{local.min.x, local.max.y, local.max.z},
				glm::vec3{local.max.x, local.min.y, local.min.z}, glm::vec3{local.max.x, local.min.y, local.max.z},
				glm::vec3{local.max.x, local.max.y, local.min.z}, glm::vec3{local.max.x, local.max.y, local.max.z}};

			for (const auto& corner : corners) {
				const glm::vec3 p = glm::vec3(world * glm::vec4(corner, 1.0f));
				min_bound = glm::min(min_bound, p);
				max_bound = glm::max(max_bound, p);
			}
		}
		return {{min_bound.x, min_bound.y, min_bound.z}, {max_bound.x, max_bound.y, max_bound.z}};
	}

	// ---- Hierarchy ----
	// Adds `node` as a child (moving it from its previous parent if it had one). Returns its id, or -1.
	int AddNode(std::shared_ptr<Node> node) {
		if (FAIL_IF(node == nullptr || node.get() == this, "invalid child node")) {
			return -1;
		}
		if (node->parent_) {
			node->parent_->RemoveNode(node->id_in_parent_);
		}
		const int id = current_node_id_++;
		node->parent_ = this;
		node->id_in_parent_ = id;
		children_.insert({id, std::move(node)});
		return id;
	}

	void RemoveNode(int id) {
		auto it = children_.find(id);
		if (it == children_.end()) {
			return;
		}
		it->second->parent_ = nullptr;
		it->second->id_in_parent_ = -1;
		children_.erase(it);
	}

	Node* GetParent() const { return parent_; }

	const std::unordered_map<int, std::shared_ptr<Node<T>>>& GetChildren() const { return children_; }

private:
	void UpdateLocalMatrix() {
		const glm::vec3 radians = glm::radians(
			glm::vec3(rotation_angles_degrees_.x, rotation_angles_degrees_.y, rotation_angles_degrees_.z));
		local_matrix_ = glm::translate(glm::mat4(1.0f), glm::vec3(position_.x, position_.y, position_.z));
		local_matrix_ = glm::rotate(local_matrix_, radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
		local_matrix_ = glm::rotate(local_matrix_, radians.y, glm::vec3(0.0f, 1.0f, 0.0f));
		local_matrix_ = glm::rotate(local_matrix_, radians.z, glm::vec3(0.0f, 0.0f, 1.0f));
		local_matrix_ = glm::scale(local_matrix_, glm::vec3(scale_.x, scale_.y, scale_.z));
	}

	std::shared_ptr<core::Model<T>> model_;
	std::shared_ptr<rendering::Material> material_;

	core::Vector3 position_ = {0, 0, 0};
	core::Vector3 scale_ = {1, 1, 1};
	core::Vector3 rotation_angles_degrees_ = {0, 0, 0};
	glm::mat4 local_matrix_ = glm::mat4(1.0f);

	Node* parent_ = nullptr;  // non-owning; cleared when the parent is destroyed or removes us
	int id_in_parent_ = -1;
	std::unordered_map<int, std::shared_ptr<Node<T>>> children_;
	int current_node_id_ = 0;
};

using Node3D = Node<core::Vertex3D>;
using Node2D = Node<core::Vertex2D>;

using Nodes2DMap = std::unordered_map<int, std::shared_ptr<Node2D>>;
using Nodes3DMap = std::unordered_map<int, std::shared_ptr<Node3D>>;

}  // namespace wlw::scene
