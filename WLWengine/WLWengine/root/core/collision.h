#pragma once
#include "../root/core/vector3.h"

namespace wlw::scene {

struct AABB {
// ... existing struct members ...
  core::Vector3 min;
  core::Vector3 max;

  bool Intersects(const AABB& other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
  }

  static AABB FromPositionAndSize(const core::Vector3& pos, float size) {
    float half = size / 2.0f;
    return {
        {pos.x - half, pos.y - half, pos.z - half},
        {pos.x + half, pos.y + half, pos.z + half}
    };
  }

  static AABB FromPositionAndScale(const core::Vector3& pos, const core::Vector3& scale) {
    core::Vector3 half = { scale.x / 2.0f, scale.y / 2.0f, scale.z / 2.0f };
    return {
        {pos.x - half.x, pos.y - half.y, pos.z - half.z},
        {pos.x + half.x, pos.y + half.y, pos.z + half.z}
    };
  }
};

} // namespace wlw::scene
