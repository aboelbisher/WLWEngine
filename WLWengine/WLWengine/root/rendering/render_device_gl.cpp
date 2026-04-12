#ifdef WLW_USE_GLFW

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "render_device.h"
#include "rendering/gl_index_buffer.h"
#include "rendering/gl_vertex_buffer.h"

namespace wlw::rendering {

class GLTexture2D : public WTexture2D {
public:
  GLTexture2D(const utils::RawImage& data) {
    glGenTextures(1, &gpu_id_);
    glBindTexture(GL_TEXTURE_2D, gpu_id_);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum format = GL_RGB;
    if (data.channels == 4) format = GL_RGBA;
    else if (data.channels == 1) format = GL_RED;

    glTexImage2D(GL_TEXTURE_2D, 0, format, (int)data.size.x, (int)data.size.y, 0, format, GL_UNSIGNED_BYTE, data.pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  ~GLTexture2D() {
    glDeleteTextures(1, &gpu_id_);
  }

  void Bind(unsigned int slot) const override {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, gpu_id_);
  }

  void Unbind() const override {
    glBindTexture(GL_TEXTURE_2D, 0);
  }
};

class GLCubemap : public WCubemap {
public:
  GLCubemap(const std::array<utils::RawImage, 6>& faces) {
    glGenTextures(1, &gpu_id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, gpu_id_);

    for (unsigned int i = 0; i < 6; i++) {
      GLenum format = GL_RGB;
      if (faces[i].channels == 4) format = GL_RGBA;
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, format, (int)faces[i].size.x, (int)faces[i].size.y, 0, format, GL_UNSIGNED_BYTE, faces[i].pixels.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  }

  ~GLCubemap() {
    glDeleteTextures(1, &gpu_id_);
  }

  void Bind(unsigned int slot) const override {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, gpu_id_);
  }

  void Unbind() const override {
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  }
};

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

  std::shared_ptr<WTexture2D> CreateTexture2D(const utils::RawImage& data) override {
    return std::make_shared<GLTexture2D>(data);
  }

  std::shared_ptr<WCubemap> CreateCubemap(const std::array<utils::RawImage, 6>& faces) override {
    return std::make_shared<GLCubemap>(faces);
  }
};

std::unique_ptr<RenderDevice> RenderDevice::Create() {
  return std::make_unique<GLRenderDevice>();
}

} // namespace wlw::rendering

#endif // WLW_USE_GLFW
