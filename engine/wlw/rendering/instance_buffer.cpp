#include "wlw/rendering/instance_buffer.h"

#include <cmath>
#include <limits>

#include "wlw/core/profile.h"

namespace wlw::rendering {

AABB InstanceBounds(std::span<const Instance> instances, const AABB& mesh_box) {
  WLW_PROFILE_FUNCTION();
  if (instances.empty()) return {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
  const glm::vec4 centre(mesh_box.Center(), 1.0f);
  const glm::vec3 half = mesh_box.Extents();

  glm::vec3 low(std::numeric_limits<float>::max());
  glm::vec3 high(std::numeric_limits<float>::lowest());
  for (const Instance& instance : instances) {
    // Per output axis i: the centre through row i (translation in .w), the reach through |row i|.
    for (int i = 0; i < 3; ++i) {
      const glm::vec4& row = instance.rows[i];
      const float middle = row.x * centre.x + row.y * centre.y + row.z * centre.z + row.w;
      const float reach = std::abs(row.x) * half.x + std::abs(row.y) * half.y + std::abs(row.z) * half.z;
      low[i] = std::min(low[i], middle - reach);
      high[i] = std::max(high[i], middle + reach);
    }
  }
  return {low, high};
}

AABB BillboardBounds(std::span<const BillboardInstance> billboards) {
  if (billboards.empty()) return {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
  glm::vec3 low(std::numeric_limits<float>::max());
  glm::vec3 high(std::numeric_limits<float>::lowest());
  for (const BillboardInstance& b : billboards) {
    // A stretched quad reaches half the stretch out on each side of its centre, plus half its width beyond the ends.
    const glm::vec3 reach = glm::abs(b.stretch) * 0.5f + glm::vec3(b.size * 0.5f);
    low = glm::min(low, b.position - reach);
    high = glm::max(high, b.position + reach);
  }
  return {low, high};
}

}  // namespace wlw::rendering
