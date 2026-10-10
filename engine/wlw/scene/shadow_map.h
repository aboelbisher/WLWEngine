#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "wlw/core/collision.h"

namespace wlw::scene {

// Where a directional light "looks" when drawing its shadow map: an orthographic box (parallel
// rays) around the things that cast shadows.
struct ShadowFrustum {
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  float texel_world = 0.0f;  // the width of one shadow-map texel in world units
  glm::mat4 ViewProjection() const { return projection * view; }
};

// Fits the light's box for a map_size x map_size shadow map:
//   across the light (the map's width and height): around the CASTERS only. A shadow can only
//     fall where a caster blocks the light, so a big floor doesn't stretch the map (and blur
//     every shadow) - the map's texels are spent where shadows can be;
//   along the light (depth): around casters AND receivers, so a receiver below the casters
//     still gets a depth to compare with.
//
// Stability matters as much as tightness: when something moves, the box changes, and if its
// edges land between texels the shadow edges crawl ("shimmering"). So the light's view is fixed
// (only its direction matters), the box's size is rounded up to whole half units, and its centre
// is snapped to whole texels: moving geometry shifts the box by exact texels, and shadows stay put.
inline ShadowFrustum FitDirectionalShadow(const glm::vec3& direction, const AABB& casters, const AABB& receivers,
                                          int map_size) {
  const glm::vec3 d = glm::normalize(direction);
  const glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
  ShadowFrustum frustum;
  frustum.view = glm::lookAt(-d, glm::vec3(0.0f), up);  // through the origin: the same for any bounds

  // A box's extent in the light's view.
  const auto light_bounds = [&frustum](const AABB& box, glm::vec3& lo, glm::vec3& hi) {
    for (int i = 0; i < 8; ++i) {
      const glm::vec3 corner{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                             (i & 4) ? box.max.z : box.min.z};
      const glm::vec3 p = glm::vec3(frustum.view * glm::vec4(corner, 1.0f));
      lo = glm::min(lo, p);
      hi = glm::max(hi, p);
    }
  };
  glm::vec3 lo(1e30f), hi(-1e30f);
  light_bounds(casters, lo, hi);
  glm::vec3 depth_lo = lo, depth_hi = hi;
  light_bounds(receivers, depth_lo, depth_hi);

  // A square box (texels stay square), its size rounded up to whole half units.
  const float extent = std::ceil(std::max({hi.x - lo.x, hi.y - lo.y, 0.5f}) * 2.0f) / 2.0f;
  frustum.texel_world = extent / static_cast<float>(std::max(map_size, 1));
  const float cx = std::floor((lo.x + hi.x) * 0.5f / frustum.texel_world) * frustum.texel_world;
  const float cy = std::floor((lo.y + hi.y) * 0.5f / frustum.texel_world) * frustum.texel_world;
  const float half = extent * 0.5f + frustum.texel_world;  // a texel of slack for the snapping

  // The view looks down -Z, so depths are -z. A margin keeps everything safely inside.
  constexpr float kMargin = 1.0f;
  frustum.projection =
      glm::ortho(cx - half, cx + half, cy - half, cy + half, -depth_hi.z - kMargin, -depth_lo.z + kMargin);
  return frustum;
}

// Casters and receivers in one box.
inline ShadowFrustum FitDirectionalShadow(const glm::vec3& direction, const AABB& bounds, int map_size) {
  return FitDirectionalShadow(direction, bounds, bounds, map_size);
}

}  // namespace wlw::scene
