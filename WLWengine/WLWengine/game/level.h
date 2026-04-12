#pragma once
#include <vector>
#include <string>
#include <memory>
#include <map>
#include "scene/window.h"
#include "scene/node.h"
#include "core/collision.h"

namespace wlw::game {

struct LevelResult {
  core::Vector3 player_start_pos;
  std::shared_ptr<scene::Node3D> player_node;
  std::vector<scene::AABB> static_colliders;
  std::map<int, std::shared_ptr<scene::Node3D>> collectibles;
};

class Level {
public:
  LevelResult Load(const std::vector<std::string>& map_data, std::shared_ptr<scene::Window> window, std::shared_ptr<rendering::Lighting> light);
};

} // namespace wlw::game
