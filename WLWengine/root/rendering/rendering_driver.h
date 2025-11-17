#pragma once

#include <memory>
#include <vector>
#include "rendering/index_buffer.h"
#include "rendering/vertex_buffer.h"
#include "core/vertex_2d.h"
#include "core/vertex_3d.h"

#include "scene/window.h"

namespace wlw::rendering {
class RenderingDriver {
public:
  virtual ~RenderingDriver() = default;

  // Resource Creation
  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) = 0;
  virtual std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex3D>& vertices) = 0;

  virtual std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) = 0;
	virtual bool Initialize(std::shared_ptr<scene::Window> window) = 0;
	virtual void AttachWindow(std::shared_ptr<scene::Window> window) = 0;
  virtual void DrawWindow(std::shared_ptr<scene::Window> window) = 0;

  virtual void SetViewport(int x, int y, int width, int height) = 0;
  virtual void Clear() = 0;


	static std::unique_ptr<RenderingDriver> Create();
};
} // wlw::core