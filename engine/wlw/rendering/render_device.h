#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>
#include "wlw/rendering/index_buffer.h"
#include "wlw/rendering/vertex_buffer.h"
#include "wlw/core/vertex_2d.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/utils/image_loader.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/render_target.h"
#include "wlw/rendering/texture.h"

namespace wlw::rendering {

class RenderDevice {
public:
  virtual ~RenderDevice() = default;

  // Resource Creation
  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) = 0;
  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex3D>& vertices) = 0;

  virtual std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) = 0;

  virtual std::shared_ptr<WTexture2D> CreateTexture2D(const utils::RawImage& data) = 0;
  // A texture from the game's own data (see TextureSettings); pixels may be null (all zero) and
  // filled later with WTexture2D::Update.
  virtual std::shared_ptr<WTexture2D> CreateTexture2D(int width, int height, const TextureSettings& settings,
                                                      const void* pixels = nullptr) = 0;
  virtual std::shared_ptr<WCubemap> CreateCubemap(const std::array<utils::RawImage, 6>& faces) = 0;

  // A game's shader from its text (see shader.h), for a ShaderMaterial. Never null; check IsValid()
  // (errors are logged).
  virtual std::shared_ptr<Shader> CreateShader(const std::string& code) = 0;

  // An offscreen picture to draw into (see render_target.h); normally made by a scene::Viewport.
  // A format the GPU can't draw into falls back to RGBA8, with a warning (see CanRenderTo).
  virtual std::shared_ptr<RenderTarget> CreateRenderTarget(int width, int height,
                                                           const RenderTargetSettings& settings = {}) = 0;
  // Whether the GPU can draw into this format. The 8-bit ones always; R16F / RGBA16F need
  // EXT_color_buffer_half_float (or EXT_color_buffer_float) on GLES 3.0, which nearly every phone
  // has. Needs the GL context.
  virtual bool CanRenderTo(TextureFormat format) const = 0;

  static std::unique_ptr<RenderDevice> Create();
};

} // namespace wlw::rendering
