#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "wlw/core/collision.h"
#include "wlw/core/logger.h"
#include "wlw/core/model.h"
#include "wlw/core/vertex_2d.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace wlw::scene {

class LightNode;  // light.h: a node that is a light source

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

	// ---- Instancing ----
	// Draws the node's model once per instance, all in ONE draw call (GPU instancing), each copy
	// with its own transform (relative to the node) and colour tint. For many copies of the same
	// mesh - particles, crowds, tiles. Calling it switches the node to instanced drawing (an empty
	// list draws nothing); ClearInstances() switches back to drawing the model once.
	void SetInstances(std::vector<rendering::InstanceData> instances) {
		instances_ = std::move(instances);
		instanced_ = true;
		++instances_version_;
	}
	void ClearInstances() {
		instances_.clear();
		instanced_ = false;
		++instances_version_;
	}
	bool IsInstanced() const { return instanced_; }
	const std::vector<rendering::InstanceData>& GetInstances() const { return instances_; }
	// For the renderer: bumped by every change, so it re-uploads only when something changed.
	uint64_t GetInstancesVersion() const { return instances_version_; }
	rendering::WInstanceBuffer* GetInstanceBuffer() const { return instance_buffer_.get(); }
	void SetInstanceBuffer(std::unique_ptr<rendering::WInstanceBuffer> buffer) { instance_buffer_ = std::move(buffer); }

	// ---- Shadows ----
	// Opt-in per node (both default to false): a caster blocks the light of shadow-casting lights
	// (it's drawn into their shadow maps), a receiver shows the shadows that fall on it.
	void SetCastsShadow(bool casts) { casts_shadow_ = casts; }
	bool CastsShadow() const { return casts_shadow_; }
	void SetReceivesShadow(bool receives) { receives_shadow_ = receives; }
	bool ReceivesShadow() const { return receives_shadow_; }

	// Light sources are nodes too (LightNode); the renderer asks every node whether it is one.
	virtual const LightNode* AsLight() const { return nullptr; }

	// ---- Visibility ----
	// Hidden nodes (and everything under them) are skipped by the renderer but stay in the scene.
	// Hiding is how you reuse objects cheaply: keep a pool of nodes and show/hide them instead of
	// creating and destroying them every time.
	void SetVisible(bool visible) { visible_ = visible; }
	bool IsVisible() const { return visible_; }
	bool IsVisibleInHierarchy() const {
		for (const Node* n = this; n != nullptr; n = n->parent_) {
			if (!n->visible_) return false;
		}
		return true;
	}

	// ---- World transform (includes all parents) ----
	glm::mat4 GetModelMatrix() const {
		return parent_ ? parent_->GetModelMatrix() * local_matrix_ : local_matrix_;
	}

	core::Vector3 GetWorldPosition() const {
		const glm::mat4 world = GetModelMatrix();
		return {world[3].x, world[3].y, world[3].z};
	}

	// World-space bounding box of this node's model (children not included). For an instanced node,
	// the box around all its instances.
	scene::AABB GetAABB() const {
		const glm::mat4 world = GetModelMatrix();
		if (!model_ || model_->meshes.empty() || (instanced_ && instances_.empty())) {
			return scene::AABB::FromPositionAndSize({world[3].x, world[3].y, world[3].z}, 0.1f);
		}

		glm::vec3 min_bound(std::numeric_limits<float>::max());
		glm::vec3 max_bound(std::numeric_limits<float>::lowest());

		auto add_corners = [&](const glm::mat4& to_world) {
			for (const auto& mesh : model_->meshes) {
				const auto& local = mesh->GetLocalAABB();
				const std::array<glm::vec3, 8> corners = {
					glm::vec3{local.min.x, local.min.y, local.min.z}, glm::vec3{local.min.x, local.min.y, local.max.z},
					glm::vec3{local.min.x, local.max.y, local.min.z}, glm::vec3{local.min.x, local.max.y, local.max.z},
					glm::vec3{local.max.x, local.min.y, local.min.z}, glm::vec3{local.max.x, local.min.y, local.max.z},
					glm::vec3{local.max.x, local.max.y, local.min.z}, glm::vec3{local.max.x, local.max.y, local.max.z}};

				for (const auto& corner : corners) {
					const glm::vec3 p = glm::vec3(to_world * glm::vec4(corner, 1.0f));
					min_bound = glm::min(min_bound, p);
					max_bound = glm::max(max_bound, p);
				}
			}
		};
		if (instanced_) {
			for (const auto& instance : instances_) add_corners(world * instance.transform);
		} else {
			add_corners(world);
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

	bool instanced_ = false;
	std::vector<rendering::InstanceData> instances_;
	uint64_t instances_version_ = 0;
	std::unique_ptr<rendering::WInstanceBuffer> instance_buffer_;

	core::Vector3 position_ = {0, 0, 0};
	core::Vector3 scale_ = {1, 1, 1};
	core::Vector3 rotation_angles_degrees_ = {0, 0, 0};
	glm::mat4 local_matrix_ = glm::mat4(1.0f);
	bool visible_ = true;
	bool casts_shadow_ = false;
	bool receives_shadow_ = false;

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
