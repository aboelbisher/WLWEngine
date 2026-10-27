#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/core/math.h"
#include "wlw/core/rotation.h"

namespace wlw::scene {

// What a node is, so code that must tell the types apart (the renderer's gather) switches on one byte instead of
// making a virtual call per node. Each class sets it in its constructor; reflection (Roadmap 4.1) will give the full
// type. Like Godot's RenderingServer instance types.
enum class NodeKind : uint8_t {
  Node,               // Node3D itself: a group / pivot, draws nothing
  MeshInstance,       // MeshInstance3D (geometry_instance.h)
  MultiMeshInstance,  // MultiMeshInstance3D (geometry_instance.h)
  SpriteBatch,        // SpriteBatch (sprite_batch.h)
  BillboardBatch,     // BillboardBatch (billboard_batch.h)
  Light,              // Light3D and its types (light.h)
  Camera,             // Camera3D (camera_3d.h)
};

// A node of the 3D scene graph: a transform (position, rotation, scale; LOCAL = relative to the parent), children,
// a name, an id, render layers and visibility. It draws nothing itself: the types built on it do (the tree below,
// like Godot's):
//
//   Node3D                      group / pivot
//   ├─ GeometryInstance3D       model, material, shadow flags, bounds (geometry_instance.h)
//   │  ├─ MeshInstance3D        draws its model once
//   │  └─ MultiMeshInstance3D   draws it once per copy, in one draw call
//   ├─ SpriteBatch              thousands of soft dots in one draw call (sprite_batch.h)
//   ├─ BillboardBatch           camera-facing quads (sparks, streaks, flares) in one draw call (billboard_batch.h)
//   ├─ Light3D                  DirectionalLight3D, PointLight3D, SpotLight3D (light.h)
//   └─ Camera3D                 a view into the scene (camera_3d.h)
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
  Node3D() : Node3D(NodeKind::Node) {}
  Node3D(const Node3D&) = delete;             // children hold a back-pointer to their parent,
  Node3D& operator=(const Node3D&) = delete;  // so copying would leave them pointing at the wrong node
  virtual ~Node3D();

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

  // Euler angles in radians (see core/rotation.h for the orders; the default ZXY = yaw, pitch, roll). GetEulerAngles
  // returns the angles last set with SetEulerAngles in the same order, exactly as given (an inspector shows 190°, not
  // its equivalent), until the rotation is changed another way; then it works them out from the quaternion.
  void SetEulerAngles(const Vec3& radians, EulerOrder order = kDefaultEulerOrder);
  Vec3 GetEulerAngles(EulerOrder order = kDefaultEulerOrder) const;

  // Turns the node by `radians` about `axis`: Rotate about its own (local) axis, RotateWorld about a world axis.
  void Rotate(const Vec3& axis, float radians);
  void RotateWorld(const Vec3& axis, float radians);

  // Moves the node along its own axes (rotation applied, not scale; like Unity's Translate and Godot's
  // translate_object_local), or along world axes.
  void Translate(const Vec3& delta);
  void TranslateWorld(const Vec3& delta);

  // Turns the node so its forward axis (+Y) points at the world point `target`, its +Z as close to `up` as it
  // can (see LookRotation for when they're parallel). Nothing happens when the target is the node's position.
  void LookAt(const Vec3& target, const Vec3& up = kUp);

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

  // The node's axes in world space, normalized: Forward = +Y, Right = +X, Up = +Z (core/math.h Conventions).
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

  // What the node is (set by its class).
  NodeKind Kind() const { return kind_; }

  // ---- Visibility ----
  // Hidden nodes (and everything under them) are skipped by the renderer but stay in the scene.
  // Hiding is how you reuse objects cheaply: keep a pool of nodes and show/hide them instead of
  // creating and destroying them every time.
  void SetVisible(bool visible) { visible_ = visible; }
  bool IsVisible() const { return visible_; }
  bool IsVisibleInHierarchy() const;

  // glm's Quat converts implicitly from a Vec3 of Euler angles (radians): without this, a 0.2-style
  // SetRotation(Vec3(0, 90, 0)) would still compile and turn by 90 radians.
  void SetRotation(const Vec3&) = delete;

 protected:
  explicit Node3D(NodeKind kind);

 private:
  void MarkLocalDirty() {
    local_dirty_ = true;
    MarkWorldDirty();
  }
  void MarkWorldDirty();
  bool IsAncestorOf(const Node3D& node) const;
  void DetachChildAt(size_t index);

  Vec3 position_{0.0f};
  Quat rotation_{1.0f, 0.0f, 0.0f, 0.0f};
  Vec3 scale_{1.0f};
  // The Euler angles last given to SetEulerAngles (GetEulerAngles returns them while euler_set_).
  Vec3 euler_{0.0f};
  EulerOrder euler_order_ = kDefaultEulerOrder;
  bool euler_set_ = false;

  mutable Mat4 local_matrix_{1.0f};
  mutable Mat4 world_matrix_{1.0f};
  // Invariant: world_dirty_ => every descendant's world_dirty_ (so marking stops at a marked node).
  mutable bool local_dirty_ = false;
  mutable bool world_dirty_ = false;

  bool visible_ = true;
  NodeKind kind_ = NodeKind::Node;
  uint32_t layers_ = 1;
  uint64_t id_;
  std::string name_;

  Node3D* parent_ = nullptr;  // non-owning; cleared when the parent is destroyed or lets go of us
  std::vector<std::shared_ptr<Node3D>> children_;
};

}  // namespace wlw::scene
