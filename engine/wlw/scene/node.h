#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/core/deprecated.h"
#include "wlw/core/geometry.h"
#include "wlw/core/math.h"
#include "wlw/core/model.h"
#include "wlw/core/rotation.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"

namespace wlw::scene {

class LightNode;    // light.h: a node that is a light source
class SpriteBatch;  // sprite_batch.h: a node that draws many soft dots in one call

// A node of the 3D scene graph: a transform (position, rotation, scale; LOCAL = relative to the parent), children,
// a name, an id, render layers and visibility. For now it also carries what it draws (model, material, instanced
// copies) and its shadow flags; Roadmap 2.3 moves those into node types (MeshInstance3D...).
//
// The transform is cached: the local matrix (T * R * S) is built when first asked for after a change, the world
// matrix (parent's world * local) the same way. A change marks the node and everything under it, stopping at
// nodes already marked, so moving a node costs nothing until something reads a matrix, and reading an unchanged
// node's matrix is a copy. Like Godot's Node3D and Unity's Transform. Game thread only: the const getters fill
// the caches.
//
// Children are kept in order (insertion order unless MoveChild changes it): hierarchy panels and scene files show
// that order. A node owns its children (shared_ptr); its parent pointer is plain. A Scene's top-level nodes are
// the children of its Root().
class Node3D {
 public:
  Node3D();
  Node3D(const Node3D&) = delete;             // children hold a back-pointer to their parent,
  Node3D& operator=(const Node3D&) = delete;  // so copying would leave them pointing at the wrong node
  virtual ~Node3D();

  // ---- Model / material ----
  // By reference: no reference-count traffic per call (the renderer asks every node every frame).
  // A reference held across SetModel / SetMaterial sees the new one.
  const std::shared_ptr<core::Model<core::Vertex3D>>& GetModel() const { return model_; }
  void SetModel(std::shared_ptr<core::Model<core::Vertex3D>> model) { model_ = std::move(model); }

  void SetMaterial(std::shared_ptr<rendering::Material> material) { material_ = std::move(material); }
  const std::shared_ptr<rendering::Material>& GetMaterial() const { return material_; }

  // ---- Local transform (relative to the parent) ----
  void SetPosition(const Vec3& position) {
    position_ = position;
    MarkLocalDirty();
  }
  const Vec3& GetPosition() const { return position_; }

  void SetScale(const Vec3& scale) {
    scale_ = scale;
    MarkLocalDirty();
  }
  const Vec3& GetScale() const { return scale_; }

  // The rotation as a quaternion (normalized when set).
  void SetRotation(const Quat& rotation);
  const Quat& GetRotation() const { return rotation_; }

  // Euler angles in radians (see core/rotation.h for the orders; YXZ = yaw, pitch, roll). GetEulerAngles returns
  // the angles last set with SetEulerAngles in the same order, exactly as given (an inspector shows 190°, not
  // its equivalent), until the rotation is changed another way; then it works them out from the quaternion.
  void SetEulerAngles(const Vec3& radians, EulerOrder order = EulerOrder::YXZ);
  Vec3 GetEulerAngles(EulerOrder order = EulerOrder::YXZ) const;

  // Turns the node by `radians` about `axis`: Rotate about its own (local) axis, RotateWorld about a world axis.
  void Rotate(const Vec3& axis, float radians);
  void RotateWorld(const Vec3& axis, float radians);

  // Moves the node along its own axes (rotation applied, not scale; like Unity's Translate and Godot's
  // translate_object_local), or along world axes.
  void Translate(const Vec3& delta);
  void TranslateWorld(const Vec3& delta);

  // Turns the node so its forward axis (-Z) points at the world point `target`, its +Y as close to `up` as it
  // can (see LookRotation for when they're parallel). Nothing happens when the target is the node's position.
  void LookAt(const Vec3& target, const Vec3& up = {0.0f, 1.0f, 0.0f});

  // T * R * S, built when asked for after a change.
  const Mat4& GetLocalMatrix() const;

  // ---- World transform (includes every parent) ----
  // Parent's world matrix * local matrix, cached until this node or a parent changes.
  const Mat4& GetWorldMatrix() const;
  Vec3 GetWorldPosition() const { return Vec3(GetWorldMatrix()[3]); }
  // The parents' rotations times this one's (non-uniform scales between them are not folded in, like Unity).
  Quat GetWorldRotation() const;
  // The parents' scales times this one's, axis by axis (exact unless a parent with a non-uniform scale is
  // rotated against its child; like Unity's lossyScale).
  Vec3 GetWorldScale() const;
  void SetWorldPosition(const Vec3& position);
  void SetWorldRotation(const Quat& rotation);

  // The node's axes in world space, normalized: Forward = -Z, Right = +X, Up = +Y.
  Vec3 Forward() const;
  Vec3 Right() const;
  Vec3 Up() const;

  // ---- Hierarchy ----
  // Adds `child` at the end of the children, taking it from its previous parent if it had one. Its local
  // transform is kept (it moves with its new parent). Refused (VERIFY) for null, this node, or one of its
  // ancestors (that would make a loop).
  void AddChild(std::shared_ptr<Node3D> child);
  // Moves `child` (one of this node's) to `index` among the children (past the end = last).
  void MoveChild(const Node3D* child, size_t index);
  // Detaches `child` and returns it (null if it isn't one of this node's children); nothing else owns it then.
  std::shared_ptr<Node3D> RemoveChild(const Node3D* child);
  // Detaches this node from its parent and returns it (null without a parent).
  std::shared_ptr<Node3D> RemoveFromParent();
  // Moves this node under `new_parent`. keep_world: its local transform is changed so it stays where it is in
  // the world (exact for position and rotation; scale only without non-uniform scales in between). It must have
  // a parent now (a node in a scene always has one: its root); a free node goes in with AddChild.
  void Reparent(Node3D& new_parent, bool keep_world = true);

  Node3D* GetParent() const { return parent_; }
  const std::vector<std::shared_ptr<Node3D>>& GetChildren() const { return children_; }
  size_t ChildCount() const { return children_.size(); }
  // Index among the parent's children (0 without a parent).
  size_t GetIndex() const;

  // ---- Names ----
  // Not unique: lookups return the first match in child order.
  void SetName(std::string name) { name_ = std::move(name); }
  const std::string& GetName() const { return name_; }
  // The first child named `name`; recursive = searched depth first through all descendants. Null if none.
  Node3D* FindChild(std::string_view name, bool recursive = true) const;
  // A path of names from this node: "Arm/Hand", ".." = the parent, "." or "" = this node. Null if any step is
  // missing.
  Node3D* GetNode(std::string_view path) const;

  // ---- Id ----
  // A random 64-bit number given at construction (never 0), for scene files, undo and node references (D15).
  // Unique in practice (2^64 values), not by guarantee: scene loading (Phase 4) checks and draws again on a
  // clash, and duplicating a node gives the copy a new id. SetId is for loading (0 is refused).
  uint64_t GetId() const { return id_; }
  void SetId(uint64_t id);

  // ---- Render layers ----
  // Up to 32 layers as bits (default: layer 0 = bit 1). A camera draws a node when they share a bit
  // (Camera3D::SetCullMask). Lights and shadows ignore layers.
  void SetLayers(uint32_t layers) { layers_ = layers; }
  uint32_t GetLayers() const { return layers_; }

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
  wlw::AABB InstanceBounds() const;
  // A fixed node-space box for the copies instead of the computed one (like Godot's custom_aabb):
  // skips the computation when the game knows where its copies can go (a particle field's volume),
  // or makes a vertex shader's displacement visible to culling. nullopt = computed again.
  void SetInstanceBounds(std::optional<wlw::AABB> box) { custom_instance_bounds_ = box; }

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
  bool IsVisibleInHierarchy() const;

  // World-space bounding box of this node's model (children not included). For an instanced node,
  // the box around all its instances (InstanceBounds, cached). Each box is moved to world space by
  // Arvo's method (wlw::TransformBox: exact for a box, 2 matrix-vector products instead of 8).
  wlw::AABB GetAABB() const;

#if !WLW_NO_DEPRECATED
  WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN
  // 0.3's rotation: degrees about X, then Y, then Z as R = Rx * Ry * Rz.
  WLW_DEPRECATED(0.3, "Use SetEulerAngles(radians, EulerOrder::XYZ) or SetRotation(Quat)")
  void SetRotation(const Vec3& degrees) { SetEulerAngles(glm::radians(degrees), EulerOrder::XYZ); }
  WLW_DEPRECATED(0.3, "Use GetWorldMatrix()")
  Mat4 GetModelMatrix() const { return GetWorldMatrix(); }
  WLW_DEPRECATED(0.3, "Use AddChild(node)")
  void AddNode(std::shared_ptr<Node3D> node) { AddChild(std::move(node)); }

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
      copies.push_back({instance.Transform(), Color32::FromPacked(instance.color).ToColor()});
    }
    return copies;
  }
  WLW_DISABLE_DEPRECATION_WARNINGS_END
#else
  // glm's Quat converts implicitly from a Vec3 of Euler angles (radians): without this, a 0.2-style
  // SetRotation(Vec3(0, 90, 0)) would still compile and turn by 90 radians.
  void SetRotation(const Vec3&) = delete;
#endif

 private:
  void MarkLocalDirty() {
    local_dirty_ = true;
    MarkWorldDirty();
  }
  void MarkWorldDirty();
  bool IsAncestorOf(const Node3D& node) const;
  void DetachChildAt(size_t index);

  // The box around all the model's meshes, in the node's space (a point at 0 without any).
  wlw::AABB ModelBox() const;

  std::shared_ptr<core::Model<core::Vertex3D>> model_;
  std::shared_ptr<rendering::Material> material_;

  bool instanced_ = false;
  std::vector<rendering::Instance> instances_;
  uint64_t instances_version_ = 0;
  std::unique_ptr<rendering::WInstanceBuffer> instance_buffer_;
  std::optional<wlw::AABB> custom_instance_bounds_;
  // InstanceBounds' cache: valid while the copies' version and the model's box are these.
  mutable wlw::AABB instance_bounds_{};
  mutable wlw::AABB instance_bounds_model_box_{};
  mutable uint64_t instance_bounds_version_ = UINT64_MAX;

  Vec3 position_{0.0f};
  Quat rotation_{1.0f, 0.0f, 0.0f, 0.0f};
  Vec3 scale_{1.0f};
  // The Euler angles last given to SetEulerAngles (GetEulerAngles returns them while euler_set_).
  Vec3 euler_{0.0f};
  EulerOrder euler_order_ = EulerOrder::YXZ;
  bool euler_set_ = false;

  mutable Mat4 local_matrix_{1.0f};
  mutable Mat4 world_matrix_{1.0f};
  // Invariant: world_dirty_ => every descendant's world_dirty_ (so marking stops at a marked node).
  mutable bool local_dirty_ = false;
  mutable bool world_dirty_ = false;

  bool visible_ = true;
  bool casts_shadow_ = false;
  bool receives_shadow_ = false;
  uint32_t layers_ = 1;
  uint64_t id_;
  std::string name_;

  Node3D* parent_ = nullptr;  // non-owning; cleared when the parent is destroyed or lets go of us
  std::vector<std::shared_ptr<Node3D>> children_;
};

}  // namespace wlw::scene
