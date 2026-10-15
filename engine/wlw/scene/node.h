#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "wlw/core/collision.h"
#include "wlw/core/assert.h"
#include "wlw/core/deprecated.h"
#include "wlw/core/model.h"
#include "wlw/core/vertex_2d.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace wlw::scene {

class LightNode;    // light.h: a node that is a light source
class SpriteBatch;  // sprite_batch.h: a node that draws many soft dots in one call

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
  // By reference: no reference-count traffic per call (the renderer asks every node every frame).
  // A reference held across SetModel / SetMaterial sees the new one.
  const std::shared_ptr<core::Model<T>>& GetModel() const { return model_; }
  void SetModel(std::shared_ptr<core::Model<T>> model) { model_ = std::move(model); }

  void SetMaterial(std::shared_ptr<rendering::Material> material) { material_ = std::move(material); }
  const std::shared_ptr<rendering::Material>& GetMaterial() const { return material_; }

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
  // with its own transform (relative to the node) and colour tint (rendering::Instance, 52 bytes).
  // For many copies of the same mesh - particles, crowds, tiles.
  //
  // WriteInstances(count) switches the node to instanced drawing (0 copies draw nothing) and returns
  // its `count` copies to fill in place, like SpriteBatch::Write: the first min(old, count) keep
  // their values, new ones start as identity + white. The span is valid until the next
  // WriteInstances / ReserveInstances / ClearInstances. Fill it right away: only a call counts as a
  // change (the renderer uploads again, the bounds are recomputed), so to change copies later, write
  // again (the same count keeps their values). Once per frame, not per copy.
  //   std::span<rendering::Instance> copies = node->WriteInstances(n);
  //   for (size_t i = 0; i < n; ++i) copies[i] = rendering::Instance::FromTransform(m[i], tint);
  // (fx::ParticleSystem::WriteTo does this for particles.) ClearInstances() switches back to drawing
  // the model once.
  std::span<rendering::Instance> WriteInstances(size_t count) {
    instances_.resize(count);
    instanced_ = true;
    ++instances_version_;
    return instances_;
  }
  // Room for `count` copies, so writes up to it never reallocate (a particle system's capacity).
  void ReserveInstances(size_t count) { instances_.reserve(count); }
  void ClearInstances() {
    instances_.clear();
    instanced_ = false;
    ++instances_version_;
  }
  bool IsInstanced() const { return instanced_; }
  std::span<const rendering::Instance> Instances() const { return instances_; }
  size_t InstanceCount() const { return instances_.size(); }
  // For the renderer: bumped by every change, so it re-uploads only when something changed.
  uint64_t GetInstancesVersion() const { return instances_version_; }

  // The box around every copy, in the node's space: SetInstanceBounds' box if one is set, else
  // computed from the copies (each copy's transform applied to the model's box) and cached until the
  // copies or the model's box change. Culling uses it (GetAABB), so a moving node costs nothing; for
  // copies that never move, one write = one computation.
  scene::AABB InstanceBounds() const {
    if (custom_instance_bounds_) return *custom_instance_bounds_;
    const scene::AABB model_box = ModelBox();
    if (instance_bounds_version_ != instances_version_ || !SameBox(instance_bounds_model_box_, model_box)) {
      instance_bounds_ = rendering::InstanceBounds(instances_, model_box);
      instance_bounds_model_box_ = model_box;
      instance_bounds_version_ = instances_version_;
    }
    return instance_bounds_;
  }
  // A fixed node-space box for the copies instead of the computed one (like Godot's custom_aabb):
  // skips the computation when the game knows where its copies can go (a particle field's volume),
  // or makes a vertex shader's displacement visible to culling. nullopt = computed again.
  void SetInstanceBounds(std::optional<scene::AABB> box) { custom_instance_bounds_ = box; }

#if !WLW_NO_DEPRECATED
  WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
  WLW_DEPRECATED(0.2, "Use WriteInstances(count) and fill the span")
  void SetInstances(const std::vector<rendering::InstanceData>& instances) {
    std::span<rendering::Instance> out = WriteInstances(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
      out[i] = rendering::Instance::FromTransform(instances[i].transform, instances[i].tint);
    }
  }
  WLW_DEPRECATED(0.2, "Use Instances() (rendering::Instance: 3 rows + RGBA8)")
  std::vector<rendering::InstanceData> GetInstances() const {
    std::vector<rendering::InstanceData> copies;
    copies.reserve(instances_.size());
    for (const rendering::Instance& instance : instances_) {
      copies.push_back({instance.Transform(), rendering::UnpackColor(instance.color)});
    }
    return copies;
  }
  WLW_DISABLE_DEPRECATION_WARNINGS_END
#endif
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
  // Sprite batches are nodes too (SpriteBatch); the renderer draws them its own way.
  virtual SpriteBatch* AsSpriteBatch() { return nullptr; }

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
  glm::mat4 GetModelMatrix() const { return parent_ ? parent_->GetModelMatrix() * local_matrix_ : local_matrix_; }

  core::Vector3 GetWorldPosition() const {
    const glm::mat4 world = GetModelMatrix();
    return {world[3].x, world[3].y, world[3].z};
  }

  // World-space bounding box of this node's model (children not included). For an instanced node,
  // the box around all its instances (InstanceBounds, cached). Each box is moved to world space by
  // Arvo's method (scene::TransformBox: exact for a box, 2 matrix-vector products instead of 8).
  scene::AABB GetAABB() const {
    const glm::mat4 world = GetModelMatrix();
    if (!model_ || model_->meshes.empty() || (instanced_ && instances_.empty())) {
      return scene::AABB::FromPositionAndSize({world[3].x, world[3].y, world[3].z}, 0.1f);
    }
    if (instanced_) return scene::TransformBox(world, InstanceBounds());
    // Mesh by mesh: as tight as the 8 corners of each (a union box first would grow under rotation).
    scene::AABB box = scene::TransformBox(world, model_->meshes.front()->GetLocalAABB());
    for (size_t i = 1; i < model_->meshes.size(); ++i) {
      box = scene::Union(box, scene::TransformBox(world, model_->meshes[i]->GetLocalAABB()));
    }
    return box;
  }

  // ---- Hierarchy ----
  // Adds `node` as a child (moving it from its previous parent if it had one). Returns its id, or -1.
  int AddNode(std::shared_ptr<Node> node) {
    if (!WLW_VERIFY(node != nullptr && node.get() != this, "invalid child node")) {
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
    const glm::vec3 radians =
        glm::radians(glm::vec3(rotation_angles_degrees_.x, rotation_angles_degrees_.y, rotation_angles_degrees_.z));
    local_matrix_ = glm::translate(glm::mat4(1.0f), glm::vec3(position_.x, position_.y, position_.z));
    local_matrix_ = glm::rotate(local_matrix_, radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    local_matrix_ = glm::rotate(local_matrix_, radians.y, glm::vec3(0.0f, 1.0f, 0.0f));
    local_matrix_ = glm::rotate(local_matrix_, radians.z, glm::vec3(0.0f, 0.0f, 1.0f));
    local_matrix_ = glm::scale(local_matrix_, glm::vec3(scale_.x, scale_.y, scale_.z));
  }

  // The box around all the model's meshes, in the node's space (a point at 0 without any).
  scene::AABB ModelBox() const {
    if (!model_ || model_->meshes.empty()) return {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    scene::AABB box = model_->meshes.front()->GetLocalAABB();
    for (size_t i = 1; i < model_->meshes.size(); ++i) box = scene::Union(box, model_->meshes[i]->GetLocalAABB());
    return box;
  }
  static bool SameBox(const scene::AABB& a, const scene::AABB& b) {
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x && a.max.y == b.max.y &&
           a.max.z == b.max.z;
  }

  std::shared_ptr<core::Model<T>> model_;
  std::shared_ptr<rendering::Material> material_;

  bool instanced_ = false;
  std::vector<rendering::Instance> instances_;
  uint64_t instances_version_ = 0;
  std::unique_ptr<rendering::WInstanceBuffer> instance_buffer_;
  std::optional<scene::AABB> custom_instance_bounds_;
  // InstanceBounds' cache: valid while the copies' version and the model's box are these.
  mutable scene::AABB instance_bounds_{};
  mutable scene::AABB instance_bounds_model_box_{};
  mutable uint64_t instance_bounds_version_ = UINT64_MAX;

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
