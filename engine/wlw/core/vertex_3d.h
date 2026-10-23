#pragma once

#include <cstddef>

#include "wlw/core/color.h"
#include "wlw/core/math.h"

namespace wlw::core {

struct Vertex3D {
  Vec3 position;
  wlw::Color color = {1.0f, 1.0f, 1.0f, 1.0f};
  Vec3 normal;
  Vec2 texCoords;
};
// The GL attribute offsets (rendering/gl_vertex_buffer.h) read this layout: 12 tightly packed floats.
static_assert(sizeof(Vertex3D) == 12 * sizeof(float));
static_assert(offsetof(Vertex3D, color) == 3 * sizeof(float) && offsetof(Vertex3D, normal) == 7 * sizeof(float) &&
              offsetof(Vertex3D, texCoords) == 10 * sizeof(float));

}  // namespace wlw::core
