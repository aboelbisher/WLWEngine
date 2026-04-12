#pragma once

#include <memory>
#include <vector>
#include <array>
#include "rendering/index_buffer.h"
#include "rendering/vertex_buffer.h"
#include "core/vertex_2d.h"
#include "core/vertex_3d.h"
#include "utils/image_loader.h"
#include "rendering/texture.h"

namespace wlw::rendering {

class RenderDevice {
public:
  virtual ~RenderDevice() = default;

  // Resource Creation
  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) = 0;
  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex3D>& vertices) = 0;

  virtual std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) = 0;

  virtual std::shared_ptr<WTexture2D> CreateTexture2D(const utils::RawImage& data) = 0;
  virtual std::shared_ptr<WCubemap> CreateCubemap(const std::array<utils::RawImage, 6>& faces) = 0;

  static std::unique_ptr<RenderDevice> Create();
};

} // namespace wlw::rendering