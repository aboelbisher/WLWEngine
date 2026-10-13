#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "wlw/core/color.h"

namespace wlw::rendering {

// One copy of a mesh in an instanced draw (see Node::SetInstances). `transform` is relative to the
// node; `tint` multiplies the mesh's vertex colours (white mesh + tint = any colour).
struct InstanceData {
  glm::mat4 transform{1.0f};
  core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

// One dot of a SpriteBatch (scene/sprite_batch.h): 16 bytes, a fifth of an InstanceData, because
// a flat round dot needs no rotation, scale or normals - only where, how big and what colour. The
// GPU builds its quad in the vertex shader (shaders/sprite.vert).
struct SpriteInstance {
  glm::vec2 position{0.0f};  // centre, on the batch node's z = 0 plane (node units)
  float size = 1.0f;         // diameter (node units)
  uint32_t color = 0xFFFFFFFFu;  // RGBA8, see PackColor: read by the GPU as 4 bytes scaled to 0..1
};
static_assert(sizeof(SpriteInstance) == 16, "the GPU reads it with a 16-byte stride");

// A colour as 4 bytes r, g, b, a in that order in memory (each 0..1 clamped, rounded).
inline uint32_t PackColor(const core::Color& color) {
  auto byte = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
  return byte(color.r) | (byte(color.g) << 8) | (byte(color.b) << 16) | (byte(color.a) << 24);
}
inline core::Color UnpackColor(uint32_t packed) {
  auto unit = [packed](int shift) { return static_cast<float>((packed >> shift) & 0xFFu) / 255.0f; };
  return {unit(0), unit(8), unit(16), unit(24)};
}

// The GPU copy of a node's instances (or a SpriteBatch's dots). Created and filled by the renderer
// the first time the node is drawn, owned by the node (like a mesh owns its vertex buffer).
class WInstanceBuffer {
 public:
  virtual ~WInstanceBuffer() = default;
};

}  // namespace wlw::rendering
