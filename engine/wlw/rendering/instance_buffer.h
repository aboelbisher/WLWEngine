#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>

#include "wlw/core/collision.h"
#include "wlw/core/color.h"
#include "wlw/core/deprecated.h"

namespace wlw::rendering {

// A colour as 4 bytes r, g, b, a in that order in memory (each 0..1 clamped, rounded to the nearest
// byte, halves up). Not std::lround: the same bytes for every float in 0..1 (checked exhaustively)
// at a fraction of the cost (it's in the particle loop).
inline uint32_t PackUnitByte(float v) {
  const float x = (v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f) * 255.0f;  // NaN -> 0
  const auto whole = static_cast<uint32_t>(x);
  return whole + (x - static_cast<float>(whole) >= 0.5f ? 1u : 0u);
}
inline uint32_t PackColor(const core::Color& color) {
  return PackUnitByte(color.r) | (PackUnitByte(color.g) << 8) | (PackUnitByte(color.b) << 16) |
         (PackUnitByte(color.a) << 24);
}
inline core::Color UnpackColor(uint32_t packed) {
  auto unit = [packed](int shift) { return static_cast<float>((packed >> shift) & 0xFFu) / 255.0f; };
  return {unit(0), unit(8), unit(16), unit(24)};
}

// One copy of a mesh in an instanced draw (see Node::WriteInstances): 52 bytes, the layout Godot's
// GLES3 renderer uses. `rows` are the top 3 rows of the copy's affine 4x4 transform, relative to the
// node (the 4th row is always 0 0 0 1, so it isn't stored): rows[i].w is the translation's i-th
// component. `color` (RGBA8, PackColor) multiplies the mesh's vertex colours (white mesh + tint =
// any colour); tints are 0..1.
// GLSL twin: shaders/wlw/instance.glsl (aInstanceRow0..2 at locations 4-6, aInstanceTint at 7).
struct Instance {
  glm::vec4 rows[3] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
  uint32_t color = 0xFFFFFFFFu;  // white

  static Instance FromTransform(const glm::mat4& transform, const core::Color& tint = {1.0f, 1.0f, 1.0f, 1.0f}) {
    Instance instance;
    instance.SetTransform(transform);
    instance.color = PackColor(tint);
    return instance;
  }
  // Takes the top 3 rows (an affine transform: the bottom row is ignored).
  void SetTransform(const glm::mat4& m) {
    for (int i = 0; i < 3; ++i) rows[i] = {m[0][i], m[1][i], m[2][i], m[3][i]};
  }
  glm::mat4 Transform() const {
    return {{rows[0].x, rows[1].x, rows[2].x, 0.0f},
            {rows[0].y, rows[1].y, rows[2].y, 0.0f},
            {rows[0].z, rows[1].z, rows[2].z, 0.0f},
            {rows[0].w, rows[1].w, rows[2].w, 1.0f}};
  }
};
static_assert(sizeof(Instance) == 52, "the GPU reads it with a 52-byte stride");
static_assert(alignof(Instance) == 4, "no padding: GLM's aligned types would break the GPU layout");
static_assert(offsetof(Instance, rows) == 0 && offsetof(Instance, color) == 48);

// The node-space box around every copy of a mesh whose own box is `mesh_box` (each copy's box by
// Arvo's method, see scene::TransformBox, from its 3x4 rows). Empty `instances`: an empty box at 0.
scene::AABB InstanceBounds(std::span<const Instance> instances, const scene::AABB& mesh_box);

#if !WLW_NO_DEPRECATED
// The 80-byte instance before 1.6: a full 4x4 matrix and a float colour.
struct WLW_DEPRECATED(0.2, "Use rendering::Instance (52 bytes: 3 rows + RGBA8)") InstanceData {
  glm::mat4 transform{1.0f};
  core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};
#endif

// One dot of a SpriteBatch (scene/sprite_batch.h): 16 bytes, a third of an Instance, because a flat
// round dot needs no rotation, scale or normals - only where, how big and what colour. The GPU
// builds its quad in the vertex shader (shaders/sprite.vert).
struct SpriteInstance {
  glm::vec2 position{0.0f};      // centre, on the batch node's z = 0 plane (node units)
  float size = 1.0f;             // diameter (node units)
  uint32_t color = 0xFFFFFFFFu;  // RGBA8, see PackColor: read by the GPU as 4 bytes scaled to 0..1
};
static_assert(sizeof(SpriteInstance) == 16, "the GPU reads it with a 16-byte stride");

// The GPU copy of a node's instances (or a SpriteBatch's dots). Created and filled by the renderer
// the first time the node is drawn, owned by the node (like a mesh owns its vertex buffer).
class WInstanceBuffer {
 public:
  virtual ~WInstanceBuffer() = default;
};

}  // namespace wlw::rendering
