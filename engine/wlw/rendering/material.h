#pragma once

#include <memory>
#include <optional>
#include <string>

#include "wlw/rendering/blend_mode.h"
#include "wlw/rendering/texture.h"
#include "wlw/core/color.h"

namespace wlw::rendering {

class ShaderMaterial;

// The engine's own look: a texture, shininess and blending; light comes from the scene's light
// nodes (scene/light.h). For a game's own shader use
// ShaderMaterial (shader_material.h), which nodes and meshes take the same way.
class Material {
 public:
  virtual ~Material() = default;

  void SetTexture(std::shared_ptr<WTexture> texture) { texture_ = std::move(texture); }

  bool HasTexture() const { return texture_ != nullptr; }
  const std::shared_ptr<WTexture>& GetTexture() const { return texture_; }

  // Non-null for a ShaderMaterial: the renderers draw it with its shader instead.
  virtual const ShaderMaterial* AsShaderMaterial() const { return nullptr; }

  static std::unique_ptr<Material> Create() { return std::make_unique<Material>(); }

  // Writes depth? Opaque materials do, the others don't, unless depth_write says otherwise.
  bool DepthWrite() const { return depth_write.value_or(blend == BlendMode::Opaque); }

  // How it blends with what's behind it (see blend_mode.h). Not Opaque: drawn after the opaque
  // objects, back to front; alpha comes from the vertex colour x texture x instance tint (a
  // ShaderMaterial: its FragColor.a, premultiplied).
  BlendMode blend = BlendMode::Opaque;
  // Depth writing: nullopt = the default for the blend mode (see DepthWrite). Turn it on for a
  // see-through object that must still hide what's drawn after it (rare).
  std::optional<bool> depth_write;
  // Draws screen offsets into the viewport's distortion buffer instead of colour into the picture (post_settings.h,
  // DistortionSettings; Unreal's distortion / refraction materials, HDRP's distortion pass): the output pass then
  // reads the picture from moved places there. Billboards (BillboardBatch: the texture's red and green are the push's
  // direction, alpha its size) and ShaderMaterials (FragColor.rg = the push) can; other draws with it are skipped.
  // Drawn only while the viewport's distortion is enabled; always Additive (pushes add up), no depth test or write.
  bool distortion = false;

  float metallic = 0.0f;
  float shininess = 32.0f;  // specular highlight sharpness under scene lights (Light3D)
  float roughness = 1.0f;
  std::string name;

 protected:
  std::shared_ptr<WTexture> texture_;
};
}  // namespace wlw::rendering