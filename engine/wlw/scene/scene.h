#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "wlw/rendering/texture.h"
#include "wlw/scene/node.h"

namespace wlw::scene {

// A world: 3D nodes (with their children), the lights among them (light nodes), and a skybox.
// It belongs to no one who looks at it: a Viewport shows a scene through a camera, and several
// viewports can show the same scene (a minimap, a second camera) - see viewport.h.
class Scene {
 public:
  static std::shared_ptr<Scene> Create() { return std::make_shared<Scene>(); }

  // Adds a top-level node (lights too); returns an id for RemoveNode.
  int AddNode(const std::shared_ptr<Node3D>& node) {
    nodes_[next_id_] = node;
    return next_id_++;
  }
  void RemoveNode(int id) { nodes_.erase(id); }
  size_t NodeCount() const { return nodes_.size(); }  // top-level nodes
  const Nodes3DMap& GetNodes() const { return nodes_; }

  void SetSkybox(std::shared_ptr<rendering::WCubemap> skybox) { skybox_ = std::move(skybox); }
  const std::shared_ptr<rendering::WCubemap>& GetSkybox() const { return skybox_; }

  // Drops every node and the skybox (frees their GPU resources if nothing else holds them).
  void Clear() {
    nodes_.clear();
    next_id_ = 0;
    skybox_ = nullptr;
  }

  // Visits every node: top-level nodes first, then their children, level by level.
  void IterateOverNodes(const std::function<void(const std::shared_ptr<Node3D>&)>& callback) const {
    std::vector<std::shared_ptr<Node3D>> current;
    current.reserve(nodes_.size());
    for (const auto& [_, node] : nodes_) current.push_back(node);

    std::vector<std::shared_ptr<Node3D>> next;
    while (!current.empty()) {
      next.clear();
      for (const auto& node : current) {
        callback(node);
        for (const auto& [_, child] : node->GetChildren()) next.push_back(child);
      }
      current.swap(next);
    }
  }

 private:
  Nodes3DMap nodes_;
  int next_id_ = 0;
  std::shared_ptr<rendering::WCubemap> skybox_;
};

}  // namespace wlw::scene
