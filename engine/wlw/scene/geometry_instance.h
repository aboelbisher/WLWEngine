#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "wlw/core/geometry.h"
#include "wlw/core/model.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"
#include "wlw/scene/node.h"

namespace wlw::scene {

// A node that draws a model: what MeshInstance3D and MultiMeshInstance3D share (like Godot's GeometryInstance3D).
// The model's meshes are drawn with their own material, or the node's where a mesh has none (the mesh's wins).
// Not made on its own: make one of the two types below.
class GeometryInstance3D : public Node3D {
 public:
  // ---- Model / material ----
  // By reference: no reference-count traffic per call (the renderer asks every node every frame).
  // A reference held across SetModel / SetMaterial sees the new one.
  const std::shared_ptr<core::Model<core::Vertex3D>>& GetModel() const { return model_; }
  void SetModel(std::shared_ptr<core::Model<core::Vertex3D>> model) { model_ = std::move(model); }

  void SetMaterial(std::shared_ptr<rendering::Material> material) { material_ = std::move(material); }
  const std::shared_ptr<rendering::Material>& GetMaterial() const { return material_; }

  // ---- Shadows ----
  // Opt-in per node (both default to false): a caster blocks the light of shadow-casting lights
  // (it's drawn into their shadow maps), a receiver shows the shadows that fall on it.
  void SetCastsShadow(bool casts) { casts_shadow_ = casts; }
  bool CastsShadow() const { return casts_shadow_; }
  void SetReceivesShadow(bool receives) { receives_shadow_ = receives; }
  bool ReceivesShadow() const { return receives_shadow_; }

  // ---- Bounds ----
  // The box around what it draws, in the node's space: the model's meshes, or for a MultiMeshInstance3D every copy
  // (InstanceBounds). A point at the origin when there is nothing to draw.
  wlw::AABB LocalBounds() const;
  // The same in world space (children not included). Each mesh's box is moved by Arvo's method (wlw::TransformBox:
  // exact for a box, 2 matrix-vector products instead of 8), so a rotated node's box stays tight. Culling and shadow
  // fitting use it once per frame. Nothing to draw: a 0.1 box at the node's position.
  wlw::AABB WorldBounds() const;

  // Whether it has anything to draw this frame (a model with meshes; a MultiMeshInstance3D also needs copies).
  bool HasGeometry() const;

  // The box around all the model's meshes, in the node's space (a point at 0 without any).
  wlw::AABB ModelBox() const;

 protected:
  explicit GeometryInstance3D(NodeKind kind) : Node3D(kind) {}

 private:
  std::shared_ptr<core::Model<core::Vertex3D>> model_;
  std::shared_ptr<rendering::Material> material_;
  bool casts_shadow_ = false;
  bool receives_shadow_ = false;
};

// Draws its model once (like Godot's MeshInstance3D, Unity's MeshRenderer).
//   auto cube = std::make_shared<scene::MeshInstance3D>();
//   cube->SetModel(utils::MakeCube());
//   scene->Root().AddChild(cube);
class MeshInstance3D : public GeometryInstance3D {
 public:
  MeshInstance3D() : GeometryInstance3D(NodeKind::MeshInstance) {}
  static std::shared_ptr<MeshInstance3D> Create() { return std::make_shared<MeshInstance3D>(); }
};

// Draws its model once per copy, all in ONE draw call (GPU instancing), each copy with its own transform (relative
// to the node) and colour tint (rendering::Instance, 52 bytes). For many copies of the same mesh: particles, crowds,
// tiles. Like Godot's MultiMeshInstance3D, except that the copies live on the node rather than in a separate
// MultiMesh resource (particles rebuild them every frame and nothing shares them). No copies = nothing drawn.
class MultiMeshInstance3D : public GeometryInstance3D {
 public:
  MultiMeshInstance3D() : GeometryInstance3D(NodeKind::MultiMeshInstance) {}
  static std::shared_ptr<MultiMeshInstance3D> Create() { return std::make_shared<MultiMeshInstance3D>(); }

  // WriteInstances(count) returns its `count` copies to fill in place, like SpriteBatch::Write: the first
  // min(old, count) keep their values, new ones start as identity + white. The span is valid until the next
  // WriteInstances / ReserveInstances / ClearInstances. Fill it right away: only a call counts as a change (the
  // renderer uploads again, the bounds are recomputed), so to change copies later, write again (the same count keeps
  // their values). Once per frame, not per copy.
  //   std::span<rendering::Instance> copies = node->WriteInstances(n);
  //   for (size_t i = 0; i < n; ++i) copies[i] = rendering::Instance::FromTransform(m[i], tint);
  // (fx::ParticleSystem::WriteTo does this for particles.)
  std::span<rendering::Instance> WriteInstances(size_t count) {
    instances_.resize(count);
    ++instances_version_;
    return instances_;
  }
  // Room for `count` copies, so writes up to it never reallocate (a particle system's capacity).
  void ReserveInstances(size_t count) { instances_.reserve(count); }
  void ClearInstances() {
    instances_.clear();
    ++instances_version_;
  }
  std::span<const rendering::Instance> Instances() const { return instances_; }
  size_t InstanceCount() const { return instances_.size(); }
  // For the renderer: bumped by every change, so it re-uploads only when something changed.
  uint64_t GetInstancesVersion() const { return instances_version_; }

  // The box around every copy, in the node's space: SetInstanceBounds' box if one is set, else computed from the
  // copies (each copy's transform applied to the model's box) and cached until the copies or the model's box change.
  // Culling uses it (WorldBounds), so a moving node costs nothing; for copies that never move, one write = one
  // computation.
  wlw::AABB InstanceBounds() const;
  // A fixed node-space box for the copies instead of the computed one (like Godot's custom_aabb): skips the
  // computation when the game knows where its copies can go (a particle field's volume), or makes a vertex shader's
  // displacement visible to culling. nullopt = computed again.
  void SetInstanceBounds(std::optional<wlw::AABB> box) { custom_instance_bounds_ = box; }

  // ---- For the renderer ----
  rendering::WInstanceBuffer* GetInstanceBuffer() const { return instance_buffer_.get(); }
  void SetInstanceBuffer(std::unique_ptr<rendering::WInstanceBuffer> buffer) { instance_buffer_ = std::move(buffer); }

 private:
  std::vector<rendering::Instance> instances_;
  uint64_t instances_version_ = 0;
  std::unique_ptr<rendering::WInstanceBuffer> instance_buffer_;
  std::optional<wlw::AABB> custom_instance_bounds_;
  // InstanceBounds' cache: valid while the copies' version and the model's box are these.
  mutable wlw::AABB instance_bounds_{};
  mutable wlw::AABB instance_bounds_model_box_{};
  mutable uint64_t instance_bounds_version_ = UINT64_MAX;
};

}  // namespace wlw::scene
