#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>
#include "wlw/rendering/index_buffer.h"
#include "wlw/rendering/vertex_buffer.h"
#include "wlw/core/vertex_3d.h"
#include "wlw/utils/image_loader.h"
#include "wlw/rendering/texture.h"

#include "wlw/rendering/render_device.h"

#include "wlw/scene/window.h"

namespace wlw::rendering {
class RenderTarget;

class RenderingDriver {
 public:
  virtual ~RenderingDriver() = default;

  virtual bool Initialize(std::shared_ptr<scene::Window> window) = 0;
  virtual void AttachWindow(std::shared_ptr<scene::Window> window) = 0;
  virtual void DrawWindow(std::shared_ptr<scene::Window> window) = 0;
  // Compiles the shader variants everything in the window's scenes needs now, so the first frames
  // don't stall on it (the Application calls it once the game has built its scenes). Variants
  // needed later still compile when first used.
  virtual void WarmUp(scene::Window& window) = 0;

  // The picture a render target holds now (e.g. a viewport's GetTarget() after a DrawWindow), as RGBA8,
  // top row first, Width() x Height() x 4 bytes; float formats are clamped to 0..1. Waits for the GPU to
  // finish drawing it (a stall): for tests and screenshots, not every frame. Empty if nothing was drawn
  // into it yet.
  virtual std::vector<uint8_t> ReadPixels(RenderTarget& target) = 0;
  // The GPU (GL_RENDERER: e.g. "Adreno (TM) 640", "llvmpipe (LLVM 17.0.6, 256 bits)"); "" before Initialize.
  virtual std::string GpuName() const = 0;

  virtual void SetViewport(int x, int y, int width, int height) = 0;
  virtual void Clear() = 0;

  virtual RenderDevice* GetDevice() = 0;

  static std::unique_ptr<RenderingDriver> Create(RenderDevice* device);
};
}  // namespace wlw::rendering