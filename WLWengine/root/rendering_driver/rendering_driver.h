#pragma once

#include <memory>
#include <vector>
#include "../core/index_buffer.h"
#include "../core/vertex_buffer.h"
#include "../core/vertex_2d.h"

namespace wlw::rendering {
class RenderingDriver {
  // Resource Creation
  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) = 0;

  // NEW: Resource Creation for Index Buffers
  virtual std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) = 0;


	static std::unique_ptr<RenderingDriver> Create();
};
} // wlw::core