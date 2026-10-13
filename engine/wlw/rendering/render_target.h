#pragma once

#include <algorithm>
#include <memory>

#include "wlw/rendering/gpu_resource.h"
#include "wlw/rendering/texture.h"

namespace wlw::rendering {

struct RenderTargetSettings {
  TextureFormat format = TextureFormat::RGBA8;  // RGBA16F: values above 1 (glow, dots that add up)
  bool smooth = true;                            // linear filtering when a shader samples it
  bool depth = false;                            // a depth buffer: needed when 3D is drawn into it
};

// An offscreen picture the GPU draws into (on GL, a framebuffer object), whose result is an
// ordinary texture: give GetTexture() to a ShaderMaterial (SetParam) like any other texture.
// Games normally don't make these directly: a scene::Viewport owns one and draws into it.
//
// Made without the GPU; the renderer makes the framebuffer when it first draws into it. A format
// the GPU can't draw into falls back to RGBA8 then, with a warning (R16F / RGBA16F need
// EXT_color_buffer_half_float on GLES 3.0, which nearly every phone has).
//
// Orientation: like every GL texture, texel row 0 is the BOTTOM of the picture. A canvas shader
// that shows a target over a rectangle samples it at vec2(vUV.x, 1.0 - vUV.y).
class RenderTarget : public Resource {
 public:
  static std::shared_ptr<RenderTarget> Create(const RenderTargetSettings& settings = {}) {
    auto target = std::make_shared<RenderTarget>(settings);
    Register(target);
    return target;
  }
  explicit RenderTarget(const RenderTargetSettings& settings)  // use Create
      : settings_(settings),
        texture_(std::make_shared<WTexture2D>(1, 1, TextureSettings{settings.format, settings.smooth},
                                              /*render_target=*/true)) {}

  // The same texture object for the target's whole life (resizing keeps it), so it can be handed
  // to materials once.
  const std::shared_ptr<WTexture2D>& GetTexture() const { return texture_; }
  int Width() const { return texture_->Width(); }
  int Height() const { return texture_->Height(); }
  const RenderTargetSettings& Settings() const { return settings_; }

  // The size to draw at; the GPU storage follows before the next draw into it (its contents are
  // lost when the size changes). Cheap when the size is the same.
  void SetSize(int width, int height) { texture_->SetRenderTargetSize(std::max(width, 1), std::max(height, 1)); }

  // ---- For the renderer ----
  // The format the GPU could draw into (RGBA8 if it couldn't draw the one asked for).
  void SetFormat(TextureFormat format) {
    settings_.format = format;
    texture_->SetFormat(format);
  }

 private:
  RenderTargetSettings settings_;
  std::shared_ptr<WTexture2D> texture_;
};

}  // namespace wlw::rendering
