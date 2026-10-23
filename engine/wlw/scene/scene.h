#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "wlw/core/deprecated.h"
#include "wlw/rendering/texture.h"
#include "wlw/scene/node.h"

namespace wlw::scene {

// A world: a tree of 3D nodes under Root() (lights are nodes too), and a skybox. It belongs to no one who looks
// at it: a Viewport shows a scene through a camera, and several viewports can show the same scene (a minimap, a
// second camera) - see viewport.h.
class Scene {
 public:
  static std::unique_ptr<Scene> Create() { return std::make_unique<Scene>(); }

  // The node every top-level node hangs under: Root().AddChild(node). It is never drawn itself; leave its
  // transform alone (it would move the whole world).
  Node3D& Root() { return root_; }
  const Node3D& Root() const { return root_; }
  size_t NodeCount() const { return root_.ChildCount(); }  // top-level nodes

  // The node with this id anywhere in the tree (a walk: for tools and loading, not every frame), or null.
  Node3D* FindById(uint64_t id) const;

  void SetSkybox(std::shared_ptr<rendering::WCubemap> skybox) { skybox_ = std::move(skybox); }
  const std::shared_ptr<rendering::WCubemap>& GetSkybox() const { return skybox_; }

  // Drops every node and the skybox (frees their GPU resources if nothing else holds them).
  void Clear();

  // Visits every node (hidden ones too, the root not), depth first in child order: a node, then its children.
  // The tree must not change during the walk.
  template <typename F>
  void ForEachNode(F&& visit) const {
    for (const std::shared_ptr<Node3D>& child : root_.GetChildren()) Walk(*child, visit);
  }

#if !WLW_NO_DEPRECATED
  WLW_DEPRECATED(0.3, "Use Root().AddChild(node)")
  void AddNode(const std::shared_ptr<Node3D>& node) { root_.AddChild(node); }
  WLW_DEPRECATED(0.3, "Use ForEachNode([](Node3D& node) { ... })")
  void IterateOverNodes(const std::function<void(const std::shared_ptr<Node3D>&)>& callback) const {
    for (const std::shared_ptr<Node3D>& child : root_.GetChildren()) WalkShared(child, callback);
  }
#endif

 private:
  template <typename F>
  static void Walk(Node3D& node, F& visit) {
    visit(node);
    for (const std::shared_ptr<Node3D>& child : node.GetChildren()) Walk(*child, visit);
  }
  static void WalkShared(const std::shared_ptr<Node3D>& node,
                         const std::function<void(const std::shared_ptr<Node3D>&)>& callback) {
    callback(node);
    for (const std::shared_ptr<Node3D>& child : node->GetChildren()) WalkShared(child, callback);
  }

  Node3D root_;
  std::shared_ptr<rendering::WCubemap> skybox_;
};

}  // namespace wlw::scene
