#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/rendering/render_list.h"

// The skybox (Scene::SetSkybox), drawn like the big engines draw their sky: AFTER the opaque
// meshes, at the far depth (1.0) with the depth test LEQUAL, so the GPU's early depth test skips
// every sky pixel an opaque mesh already covers; transparent meshes come after it and blend over
// it. The sky itself is one triangle over the whole viewport (skybox.vert, no vertex buffer), each
// corner's view ray from SkyRayMatrix. No GPU here: tested on the CPU.
namespace wlw::rendering {

// Clip space -> world-space view ray, for the sky: ray = (SkyRayMatrix * vec4(ndc.xy, 1, 1)).xyz.
// The view's translation is dropped (the sky stays centred on the camera). Its w is a positive
// constant across the far plane (1 / far for a perspective, 1 for an orthographic projection), so
// the xyz alone point the right way and interpolate exactly between the triangle's corners.
inline glm::mat4 SkyRayMatrix(const glm::mat4& view, const glm::mat4& projection) {
  return glm::mat4(glm::inverse(glm::mat3(view))) * glm::inverse(projection);
}

// Where the sky goes in a sorted draw order: before the first transparent draw (after every opaque
// one), or first when the target has no depth buffer (nothing to test against: drawn later, it
// would cover the meshes).
inline size_t SkyDrawPosition(const std::vector<DrawOrder>& sorted, bool has_depth) {
  if (!has_depth) return 0;
  const auto first_transparent = std::partition_point(
      sorted.begin(), sorted.end(), [](const DrawOrder& order) { return !IsTransparentKey(order.key); });
  return static_cast<size_t>(first_transparent - sorted.begin());
}

}  // namespace wlw::rendering
