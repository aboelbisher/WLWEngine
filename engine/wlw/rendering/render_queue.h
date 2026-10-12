#pragma once

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/rendering/blend_mode.h"

namespace wlw::rendering {

// The two passes of a viewport's main pass. Opaque first, in any order (the depth buffer sorts
// them per pixel); then transparent, sorted back to front, testing depth but not writing it, so
// they show what's behind them and blend over it.
enum class RenderQueue { Opaque, Transparent };

inline RenderQueue QueueFor(BlendMode blend) {
  return blend == BlendMode::Opaque ? RenderQueue::Opaque : RenderQueue::Transparent;
}

// Sorts items farthest from the camera first. `center(item)` is a world position (the middle of
// its bounding box); `view` is the camera's view matrix, so distance is measured along the
// camera's direction, which is right for perspective and orthographic cameras alike. An instanced
// node counts as one item: its copies aren't sorted among themselves (fine for Additive, where
// order doesn't matter).
template <typename Item, typename CenterOf>
void SortBackToFront(std::vector<Item>& items, const glm::mat4& view, CenterOf center) {
  // In view space the camera looks down -z: the more negative z, the farther away.
  auto view_z = [&view](const glm::vec3& world) { return (view * glm::vec4(world, 1.0f)).z; };
  std::stable_sort(items.begin(), items.end(),
                   [&](const Item& a, const Item& b) { return view_z(center(a)) < view_z(center(b)); });
}

}  // namespace wlw::rendering
