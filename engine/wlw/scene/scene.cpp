#include "wlw/scene/scene.h"

namespace wlw::scene {

Node3D* Scene::FindById(uint64_t id) const {
  Node3D* found = nullptr;
  ForEachNode([&](Node3D& node) {
    if (found == nullptr && node.GetId() == id) found = &node;
  });
  return found;
}

void Scene::Clear() {
  while (root_.ChildCount() > 0) root_.RemoveChild(root_.GetChildren().back().get());
  skybox_ = nullptr;
}

}  // namespace wlw::scene
