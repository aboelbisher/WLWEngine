#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl.h"
#include "render_device.h"
#include <vector>

#include "wlw/rendering/gl_index_buffer.h"
#include "wlw/rendering/gl_shader.h"
#include "wlw/rendering/gl_vertex_buffer.h"
#include "wlw/platform/platform.h"

namespace wlw::rendering {

namespace {

// How each TextureFormat is stored and uploaded.
struct GLFormat {
  GLint internal_format;
  GLenum format;
  GLenum type;
  size_t texel_bytes;  // of the data the game passes in
};

GLFormat ToGL(TextureFormat format) {
  switch (format) {
    case TextureFormat::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
    case TextureFormat::RGB8: return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3};
    case TextureFormat::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
    case TextureFormat::R16F: return {GL_R16F, GL_RED, GL_FLOAT, 4};
    case TextureFormat::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_FLOAT, 16};
  }
  return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
}

}  // namespace

class GLTexture2D : public WTexture2D {
public:
  // An image file's texture: repeating, mipmapped (it may be drawn small, far away in 3D).
  explicit GLTexture2D(const utils::RawImage& data)
      : GLTexture2D(static_cast<int>(data.size.x), static_cast<int>(data.size.y),
                    data.channels == 4   ? TextureFormat::RGBA8
                    : data.channels == 1 ? TextureFormat::R8
                                         : TextureFormat::RGB8,
                    /*smooth=*/true, /*repeat=*/true, /*mipmaps=*/true, data.pixels.data()) {}

  GLTexture2D(int width, int height, TextureFormat format, bool smooth, bool repeat, bool mipmaps, const void* pixels)
      : mipmaps_(mipmaps) {
    width_ = width;
    height_ = height;
    format_ = format;
    glGenTextures(1, &gpu_id_);
    glBindTexture(GL_TEXTURE_2D, gpu_id_);
    const GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmaps ? GL_LINEAR_MIPMAP_LINEAR : (smooth ? GL_LINEAR : GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);

    // Sized internal formats (GL_RGB8...) are required by GLES 3.0 and fine on desktop GL.
    const GLFormat gl = ToGL(format);
    std::vector<unsigned char> zeros;
    if (!pixels) {  // GL leaves a texture without data undefined: start it at zero
      zeros.assign(static_cast<size_t>(width) * height * gl.texel_bytes, 0);
      pixels = zeros.data();
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // rows of RGB / R8 images aren't always 4-byte aligned
    glTexImage2D(GL_TEXTURE_2D, 0, gl.internal_format, width, height, 0, gl.format, gl.type, pixels);
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
  }

  ~GLTexture2D() {
    if (platform::HasCurrentGLContext()) glDeleteTextures(1, &gpu_id_);
  }

  void Bind(unsigned int slot) const override {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, gpu_id_);
  }

  void Unbind() const override {
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void Update(const void* pixels) override {
    const GLFormat gl = ToGL(format_);
    glBindTexture(GL_TEXTURE_2D, gpu_id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, gl.format, gl.type, pixels);
    if (mipmaps_) glGenerateMipmap(GL_TEXTURE_2D);
  }

private:
  bool mipmaps_ = false;
};

class GLCubemap : public WCubemap {
public:
  GLCubemap(const std::array<utils::RawImage, 6>& faces) {
    glGenTextures(1, &gpu_id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, gpu_id_);

    for (unsigned int i = 0; i < 6; i++) {
      GLenum format = GL_RGB;
      GLint internal_format = GL_RGB8;
      if (faces[i].channels == 4) { format = GL_RGBA; internal_format = GL_RGBA8; }
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internal_format, (int)faces[i].size.x, (int)faces[i].size.y, 0, format, GL_UNSIGNED_BYTE, faces[i].pixels.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  }

  ~GLCubemap() {
    if (platform::HasCurrentGLContext()) glDeleteTextures(1, &gpu_id_);
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

  std::shared_ptr<WTexture2D> CreateTexture2D(int width, int height, const TextureSettings& settings,
                                              const void* pixels) override {
    return std::make_shared<GLTexture2D>(width, height, settings.format, settings.smooth, settings.repeat,
                                         /*mipmaps=*/false, pixels);
  }

  std::shared_ptr<Shader> CreateShader(const std::string& code) override {
    return std::make_shared<GLShader>(code);
  }

  std::shared_ptr<WCubemap> CreateCubemap(const std::array<utils::RawImage, 6>& faces) override {
    return std::make_shared<GLCubemap>(faces);
  }
};

std::unique_ptr<RenderDevice> RenderDevice::Create() {
  return std::make_unique<GLRenderDevice>();
}

} // namespace wlw::rendering

#endif // WLW_RENDERER_GL
