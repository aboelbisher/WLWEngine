#include "rendering_driver.h"
#include "../core/gl_index_buffer.h"
#include "../core/gl_vertex_buffer.h"

namespace wlw::rendering {

class GLRenderingDriver : public RenderingDriver {
public:

	std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) override { 
		return std::make_unique<core::GLVertexBuffer>(vertices);
	}

	// NEW: Resource Creation for Index Buffers
	std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) override { 
		return std::make_unique<core::GLIndexBuffer>(indices);
	}

};

std::unique_ptr<RenderingDriver> RenderingDriver::Create() {
	return std::make_unique<GLRenderingDriver>();
}

} // wlw::core