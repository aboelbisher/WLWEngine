#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl.h"
#include "render_device.h"
#include <algorithm>
#include <cstring>
#include <vector>

#include "wlw/core/log.h"

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

  // New storage at another size, contents undefined (a render target that is drawn over next).
  // The texture object stays the same, so materials holding it keep working.
  void Reallocate(int width, int height) {
    width_ = width;
    height_ = height;
    const GLFormat gl = ToGL(format_);
    glBindTexture(GL_TEXTURE_2D, gpu_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, gl.internal_format, width, height, 0, gl.format, gl.type, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
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

// A framebuffer object: a colour texture (GLTexture2D, handed out as the target's texture) plus,
// if asked for, a depth renderbuffer (never sampled, so it needn't be a texture).
class GLRenderTarget : public RenderTarget {
public:
  GLRenderTarget(int width, int height, const RenderTargetSettings& settings) {
    settings_ = settings;
    color_ = std::make_shared<GLTexture2D>(1, 1, settings.format, settings.smooth, /*repeat=*/false,
                                           /*mipmaps=*/false, nullptr);
    texture_ = color_;
    glGenFramebuffers(1, &framebuffer_id_);
    if (settings.depth) glGenRenderbuffers(1, &depth_);
    EnsureSize(width, height);
  }

  ~GLRenderTarget() override {
    if (!platform::HasCurrentGLContext()) return;
    glDeleteFramebuffers(1, &framebuffer_id_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
  }

  void EnsureSize(int width, int height) override {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    color_->Reallocate(width, height);
    if (depth_) {
      glBindRenderbuffer(GL_RENDERBUFFER, depth_);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
      glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    GLint previous_framebuffer = 0;  // not always 0: the platform may draw into its own
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_->GetID(), 0);
    if (depth_) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      WLW_LOG_ERROR("Render target %d x %d incomplete (status 0x%x): it will stay empty", width, height, status);
    }
  }

private:
  std::shared_ptr<GLTexture2D> color_;
  GLuint depth_ = 0;
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

  std::shared_ptr<RenderTarget> CreateRenderTarget(int width, int height,
                                                   const RenderTargetSettings& settings) override {
    RenderTargetSettings checked = settings;
    if (!CanRenderTo(checked.format)) {
      WLW_LOG_WARN("This GPU can't draw into 16-bit float textures: render target falls back to RGBA8");
      checked.format = TextureFormat::RGBA8;
    }
    return std::make_shared<GLRenderTarget>(width, height, checked);
  }

  bool CanRenderTo(TextureFormat format) const override {
    if (format != TextureFormat::R16F && format != TextureFormat::RGBA16F) return true;
#if defined(WLW_GLES)
    if (half_float_renderable_ < 0) {  // asked once: the answer doesn't change
      half_float_renderable_ = 0;
      GLint count = 0;
      glGetIntegerv(GL_NUM_EXTENSIONS, &count);
      for (GLint i = 0; i < count; ++i) {
        const auto* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (name && (std::strcmp(name, "GL_EXT_color_buffer_half_float") == 0 ||
                     std::strcmp(name, "GL_EXT_color_buffer_float") == 0)) {
          half_float_renderable_ = 1;
          break;
        }
      }
    }
    return half_float_renderable_ == 1;
#else
    return true;  // desktop GL 3.3: float formats are colour-renderable in core
#endif
  }

private:
  mutable int half_float_renderable_ = -1;  // -1 = not asked yet
};

std::unique_ptr<RenderDevice> RenderDevice::Create() {
  return std::make_unique<GLRenderDevice>();
}

} // namespace wlw::rendering

#endif // WLW_RENDERER_GL
