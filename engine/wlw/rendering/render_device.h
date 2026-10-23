#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "wlw/core/vertex_3d.h"
#include "wlw/rendering/index_buffer.h"
#include "wlw/rendering/vertex_buffer.h"

namespace wlw::rendering {

// The renderer's GPU buffers for meshes (made when a mesh is first drawn). Engine-internal: games
// never see a device. Textures, shaders and render targets are made with their own Create
// functions (texture.h, shader.h, render_target.h) and get their GPU side from the renderer.
class RenderDevice {
 public:
  virtual ~RenderDevice() = default;

  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex3D>& vertices) = 0;

  virtual std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) = 0;

  static std::unique_ptr<RenderDevice> Create();
};

}  // namespace wlw::rendering
