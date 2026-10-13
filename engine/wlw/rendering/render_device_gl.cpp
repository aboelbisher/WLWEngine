#ifdef WLW_RENDERER_GL

#include "render_device.h"

#include "wlw/rendering/gl_index_buffer.h"
#include "wlw/rendering/gl_vertex_buffer.h"

namespace wlw::rendering {

class GLRenderDevice : public RenderDevice {
public:
  std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex2D>& vertices) override {
    return std::make_unique<core::GLVertexBuffer>(vertices);
  }

  std::unique_ptr<core::WVertexBuffer> CreateVertexBuffer(const std::vector<core::Vertex3D>& vertices) override {
    return std::make_unique<core::GLVertexBuffer>(vertices);
  }

  std::unique_ptr<core::WIndexBuffer> CreateIndexBuffer(const std::vector<uint32_t>& indices) override {
    return std::make_unique<core::GLIndexBuffer>(indices);
  }
};

std::unique_ptr<RenderDevice> RenderDevice::Create() {
  return std::make_unique<GLRenderDevice>();
}

} // namespace wlw::rendering

#endif // WLW_RENDERER_GL
