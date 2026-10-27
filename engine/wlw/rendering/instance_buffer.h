#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/core/geometry.h"

namespace wlw::rendering {

// One copy of a mesh in an instanced draw (see MultiMeshInstance3D::WriteInstances): 52 bytes, the layout Godot's
// GLES3 renderer uses. `rows` are the top 3 rows of the copy's affine 4x4 transform, relative to the
// node (the 4th row is always 0 0 0 1, so it isn't stored): rows[i].w is the translation's i-th
// component. `color` (RGBA8, Color32::Packed) multiplies the mesh's vertex colours (white mesh + tint =
// any colour); tints are 0..1.
// GLSL twin: shaders/wlw/instance.glsl (aInstanceRow0..2 at locations 4-6, aInstanceTint at 7).
struct Instance {
  glm::vec4 rows[3] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
  uint32_t color = 0xFFFFFFFFu;  // white

  static Instance FromTransform(const glm::mat4& transform, const Color& tint = {1.0f, 1.0f, 1.0f, 1.0f}) {
    Instance instance;
    instance.SetTransform(transform);
    instance.color = Color32(tint).Packed();
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
// Arvo's method, see TransformBox, from its 3x4 rows). Empty `instances`: an empty box at 0.
AABB InstanceBounds(std::span<const Instance> instances, const AABB& mesh_box);

// One dot of a SpriteBatch (scene/sprite_batch.h): 16 bytes, a third of an Instance, because a flat
// round dot needs no rotation, scale or normals - only where, how big and what colour. The GPU
// builds its quad in the vertex shader (shaders/sprite.vert).
struct SpriteInstance {
  glm::vec2 position{0.0f};      // centre, on the batch node's z = 0 plane (node units)
  float size = 1.0f;             // diameter (node units)
  uint32_t color = 0xFFFFFFFFu;  // RGBA8, see Color32::Packed: read by the GPU as 4 bytes scaled to 0..1
};
static_assert(sizeof(SpriteInstance) == 16, "the GPU reads it with a 16-byte stride");

// One quad of a BillboardBatch (scene/billboard_batch.h): 40 bytes. A billboard is a quad that always faces the
// camera, placed anywhere in 3D: a spark, a flare, a mote of light. Round ones turn by `rotation`; a non-zero
// `stretch` lays the quad along that vector's direction ON SCREEN and makes it longer by the vector's length there
// (a streak: pass velocity x seconds, like Unity's Stretched Billboard or Niagara's velocity-aligned sprites).
// `intensity` multiplies the colour: RGBA8 stops at 1, an HDR picture with bloom doesn't (2 = twice as bright).
// The GPU builds the quad in the vertex shader (shaders/billboard.vert).
struct BillboardInstance {
  glm::vec3 position{0.0f};      // centre (node units)
  float size = 1.0f;             // width, and the length of an unstretched quad (node units)
  glm::vec3 stretch{0.0f};       // node units; zero = a plain camera-facing quad
  float rotation = 0.0f;         // radians, counter-clockwise on screen (only when not stretched)
  uint32_t color = 0xFFFFFFFFu;  // RGBA8, see Color32::Packed: read by the GPU as 4 bytes scaled to 0..1
  float intensity = 1.0f;        // multiplies rgb (HDR)
};
static_assert(sizeof(BillboardInstance) == 40, "the GPU reads it with a 40-byte stride");
static_assert(offsetof(BillboardInstance, stretch) == 16 && offsetof(BillboardInstance, color) == 32 &&
              offsetof(BillboardInstance, intensity) == 36);

// The box around billboards in node units: each one's centre -+ (half its size + half its |stretch|) per axis, what
// its quad can cover whichever way the camera looks. Empty `billboards`: an empty box at 0.
AABB BillboardBounds(std::span<const BillboardInstance> billboards);

// The GPU copy of a node's instances (or a SpriteBatch's dots). Created and filled by the renderer
// the first time the node is drawn, owned by the node (like a mesh owns its vertex buffer).
class WInstanceBuffer {
 public:
  virtual ~WInstanceBuffer() = default;
};

}  // namespace wlw::rendering
