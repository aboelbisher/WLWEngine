#pragma once

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/scene/light.h"

namespace wlw::rendering {

// Lights of each type the frame block holds (WLW_MAX_LIGHTS_PER_TYPE in wlw/frame.glsl).
inline constexpr int kMaxLightsPerType = 4;

// The per-frame uniform block, uploaded once per viewport: the C++ twin of `WlwFrame` in
// shaders/wlw/frame.glsl (std140). Every member is a multiple of 16 bytes, so C++ offsets are
// the std140 ones (tested in shader_tests); change both together.
struct FrameUniforms {
  struct Directional {
    glm::vec4 direction_ambient{0.0f};  // xyz: where it shines (normalized); w: ambient
    glm::vec4 color{0.0f};              // rgb: colour x intensity
  };
  struct Point {
    glm::vec4 position_ambient{0.0f};  // xyz: world position; w: ambient
    glm::vec4 color{0.0f};
    glm::vec4 attenuation{1.0f, 0.0f, 0.0f, 0.0f};  // constant, linear, quadratic
  };
  struct Spot {
    glm::vec4 position_ambient{0.0f};
    glm::vec4 direction{0.0f};
    glm::vec4 color{0.0f};
    glm::vec4 attenuation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 cone{-1.0f, -1.0f, 0.0f, 0.0f};  // cos(inner), cos(outer)
  };

  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  glm::mat4 view_projection{1.0f};
  glm::mat4 light_space{1.0f};           // the shadow light's view-projection
  glm::vec4 camera_position{0.0f};       // xyz; w: 1 = orthographic camera
  glm::vec4 view_forward{0.0f};          // xyz; w: shadow strength
  glm::vec4 shadow_params{0.0f};         // filter radius (texels), 1 / map size, normal offset (world), -
  glm::ivec4 light_counts{0, 0, 0, -1};  // directional, point, spot; w: the shadow light (directional index, -1 = none)
  Directional directional[kMaxLightsPerType];
  Point point[kMaxLightsPerType];
  Spot spot[kMaxLightsPerType];
};
static_assert(sizeof(FrameUniforms) == 960, "must match WlwFrame's std140 size (wlw/frame.glsl)");

// A falloff that is down to about 1% at `range` (the classic constant / linear / quadratic form);
// range 0 = no falloff.
inline glm::vec4 AttenuationFor(float range) {
  return range > 0.0f ? glm::vec4(1.0f, 4.5f / range, 75.0f / (range * range), 0.0f)
                      : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
}

// Packs the visible light nodes into `frame` by type, in order, at most kMaxLightsPerType of each
// (the rest are dropped). `shadow_light` (one of `lights`, or null) is the light whose shadow map
// is drawn: it must be directional; its index goes to light_counts.w.
inline void SetFrameLights(FrameUniforms& frame, const std::vector<const scene::Light3D*>& lights,
                           const scene::Light3D* shadow_light) {
  frame.light_counts = {0, 0, 0, -1};
  for (const scene::Light3D* light : lights) {
    const glm::vec3 color = light->GetColor().Rgb() * light->GetIntensity();
    const glm::vec3 position = light->GetWorldPosition();
    switch (light->GetType()) {
      case scene::Light3D::Type::Directional: {
        int& count = frame.light_counts.x;
        if (count >= kMaxLightsPerType) break;
        if (light == shadow_light) frame.light_counts.w = count;
        frame.directional[count++] = {glm::vec4(light->GetDirection(), light->GetAmbient()), glm::vec4(color, 0.0f)};
        break;
      }
      case scene::Light3D::Type::Point: {
        int& count = frame.light_counts.y;
        if (count >= kMaxLightsPerType) break;
        const float range = static_cast<const scene::PointLight3D*>(light)->GetRange();
        frame.point[count++] = {glm::vec4(position, light->GetAmbient()), glm::vec4(color, 0.0f),
                                AttenuationFor(range)};
        break;
      }
      case scene::Light3D::Type::Spot: {
        int& count = frame.light_counts.z;
        if (count >= kMaxLightsPerType) break;
        const auto* spot = static_cast<const scene::SpotLight3D*>(light);
        const glm::vec4 cone(std::cos(spot->GetInnerAngle()), std::cos(spot->GetOuterAngle()), 0.0f, 0.0f);
        frame.spot[count++] = {glm::vec4(position, light->GetAmbient()), glm::vec4(light->GetDirection(), 0.0f),
                               glm::vec4(color, 0.0f), AttenuationFor(spot->GetRange()), cone};
        break;
      }
    }
  }
}

}  // namespace wlw::rendering
