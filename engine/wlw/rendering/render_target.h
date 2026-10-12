#pragma once

#include <memory>

#include "wlw/rendering/texture.h"

namespace wlw::rendering {

struct RenderTargetSettings {
  TextureFormat format = TextureFormat::RGBA8;  // RGBA16F: values above 1 (glow, dots that add up)
  bool smooth = true;                            // linear filtering when a shader samples it
  bool depth = false;                            // a depth buffer: needed when 3D is drawn into it
};

// An offscreen picture the GPU draws into (an OpenGL framebuffer object), whose result is an
// ordinary texture: give GetTexture() to a ShaderMaterial (SetParam) like any other texture.
// Games normally don't make these directly: a scene::Viewport owns one and draws into it.
//
// Orientation: like every GL texture, texel row 0 is the BOTTOM of the picture. A canvas shader
// that shows a target over a rectangle samples it at vec2(vUV.x, 1.0 - vUV.y).
class RenderTarget {
 public:
  virtual ~RenderTarget() = default;

  // The same texture object for the target's whole life (resizing keeps it), so it can be handed
  // to materials once.
  const std::shared_ptr<WTexture2D>& GetTexture() const { return texture_; }
  int Width() const { return width_; }
  int Height() const { return height_; }
  const RenderTargetSettings& Settings() const { return settings_; }

  // Resizes the target (its contents are lost); does nothing when the size is the same. Needs the
  // GL context: the renderer calls it before drawing into the target.
  virtual void EnsureSize(int width, int height) = 0;

  unsigned int FramebufferId() const { return framebuffer_id_; }  // for the renderer

 protected:
  std::shared_ptr<WTexture2D> texture_;
  int width_ = 0;
  int height_ = 0;
  RenderTargetSettings settings_;
  unsigned int framebuffer_id_ = 0;
};

}  // namespace wlw::rendering
