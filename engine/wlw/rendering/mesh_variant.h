#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The engine's 3D shader (shaders/mesh.vert + mesh.frag) is compiled in variants: one program per
// combination of these features actually used, picked per draw, so the shaders have #ifdefs
// instead of runtime ifs. A key is the features' bits.
namespace wlw::rendering {

enum MeshFeature : uint32_t {
  kMeshInstanced = 1u << 0,  // INSTANCED: many copies in one draw (per-copy transform + tint)
  kMeshTextured = 1u << 1,   // TEXTURED: the material has a texture
  kMeshLit = 1u << 2,        // LIT: the scene has light nodes
  kMeshShadows = 1u << 3,    // SHADOWS: receives the shadow light's shadow (only with LIT)
  kMeshBlend = 1u << 4,      // BLEND: the material blends (else alpha is 1)
};

// What decides a draw's variant.
struct MeshDraw {
  bool instanced = false;
  bool textured = false;
  bool scene_lit = false;        // the viewport's scene has visible light nodes
  bool shadow_active = false;    // ...and a shadow map was drawn this frame
  bool receives_shadow = false;  // the node opted in (Node3D::SetReceivesShadow)
  bool blends = false;           // its material isn't BlendMode::Opaque
};

inline uint32_t MeshVariantKey(const MeshDraw& draw) {
  uint32_t key = 0;
  if (draw.instanced) key |= kMeshInstanced;
  if (draw.textured) key |= kMeshTextured;
  if (draw.scene_lit) key |= kMeshLit;
  if (draw.scene_lit && draw.shadow_active && draw.receives_shadow) key |= kMeshShadows;
  if (draw.blends) key |= kMeshBlend;
  return key;
}

// The #defines for a key, in a fixed order.
inline std::vector<std::string> MeshVariantDefines(uint32_t key) {
  std::vector<std::string> defines;
  if (key & kMeshInstanced) defines.emplace_back("INSTANCED");
  if (key & kMeshTextured) defines.emplace_back("TEXTURED");
  if (key & kMeshLit) defines.emplace_back("LIT");
  if (key & kMeshShadows) defines.emplace_back("SHADOWS");
  if (key & kMeshBlend) defines.emplace_back("BLEND");
  return defines;
}

}  // namespace wlw::rendering
