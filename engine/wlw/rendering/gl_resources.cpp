#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl_resources.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl_shader.h"

namespace wlw::rendering {

namespace {

// How each TextureFormat is stored and uploaded.
struct GLFormat {
  GLint internal_format;
  GLenum format;
  GLenum type;
};

GLFormat ToGL(TextureFormat format) {
  // Sized internal formats (GL_RGB8...) are required by GLES 3.0 and fine on desktop GL.
  switch (format) {
    case TextureFormat::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
    case TextureFormat::RGB8: return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
    case TextureFormat::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case TextureFormat::R16F: return {GL_R16F, GL_RED, GL_FLOAT};
    case TextureFormat::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_FLOAT};
  }
  return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

bool warmed_up = false;  // PrepareAliveResources ran: later compiles are worth a log line

// A texture's GL object, and what it holds now.
class GLTextureObject : public GpuResource {
 public:
  explicit GLTextureObject(GLenum kind) : kind_(kind) { glGenTextures(1, &id_); }
  ~GLTextureObject() override {
    if (platform::HasCurrentGLContext()) glDeleteTextures(1, &id_);
  }
  GLuint id_ = 0;
  GLenum kind_;
  int width_ = 0, height_ = 0;
  TextureFormat format_ = TextureFormat::RGBA8;
  uint64_t version_ = 0;  // the WTexture2D::Version() on the GPU
};

void SetSampling(GLenum kind, bool smooth, bool repeat, bool mipmaps) {
  const GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(kind, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(kind, GL_TEXTURE_WRAP_T, wrap);
  if (kind == GL_TEXTURE_CUBE_MAP) glTexParameteri(kind, GL_TEXTURE_WRAP_R, wrap);
  glTexParameteri(kind, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : (smooth ? GL_LINEAR : GL_NEAREST));
  glTexParameteri(kind, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
}

// Brings a 2D texture's GL object up to date (binds it to the active unit while doing so).
void Prepare2D(const WTexture2D& texture, GLTextureObject& gl, bool created) {
  const TextureSettings& settings = texture.Settings();
  const GLFormat format = ToGL(settings.format);
  if (texture.IsRenderTarget()) {
    // Storage only, no texels: the GPU draws them. Reallocated when the target's size or format
    // changed (the texture object stays, so materials and framebuffers holding it keep working).
    if (!created && gl.width_ == texture.Width() && gl.height_ == texture.Height() && gl.format_ == settings.format) {
      return;
    }
    glBindTexture(GL_TEXTURE_2D, gl.id_);
    if (created) SetSampling(GL_TEXTURE_2D, settings.smooth, /*repeat=*/false, /*mipmaps=*/false);
    glTexImage2D(GL_TEXTURE_2D, 0, format.internal_format, texture.Width(), texture.Height(), 0, format.format,
                 format.type, nullptr);
  } else {
    if (!created && gl.version_ == texture.Version()) return;
    glBindTexture(GL_TEXTURE_2D, gl.id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // rows of RGB / R8 images aren't always 4-byte aligned
    const void* pixels = texture.Pixels().empty() ? nullptr : texture.Pixels().data();
    if (created) {
      SetSampling(GL_TEXTURE_2D, settings.smooth, settings.repeat, settings.mipmaps);
      std::vector<uint8_t> zeros;
      if (!pixels) {  // GL leaves a texture without data undefined: start it at zero
        zeros.assign(static_cast<size_t>(texture.Width()) * texture.Height() * TexelBytes(settings.format), 0);
        pixels = zeros.data();
      }
      glTexImage2D(GL_TEXTURE_2D, 0, format.internal_format, texture.Width(), texture.Height(), 0, format.format,
                   format.type, pixels);
    } else if (pixels) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture.Width(), texture.Height(), format.format, format.type, pixels);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (settings.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    gl.version_ = texture.Version();
    texture.ReleasePixels();  // on the GPU now: no second copy in memory
  }
  gl.width_ = texture.Width();
  gl.height_ = texture.Height();
  gl.format_ = settings.format;
}

void PrepareCubemap(const WCubemap& cubemap, GLTextureObject& gl) {
  glBindTexture(GL_TEXTURE_CUBE_MAP, gl.id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const auto& faces = cubemap.Faces();
  for (size_t i = 0; i < faces.size() && i < 6; ++i) {
    const GLFormat format = ToGL(faces[i].channels == 4 ? TextureFormat::RGBA8 : TextureFormat::RGB8);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(i), 0, format.internal_format,
                 static_cast<GLsizei>(faces[i].size.x), static_cast<GLsizei>(faces[i].size.y), 0, format.format,
                 format.type, faces[i].pixels.data());
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  SetSampling(GL_TEXTURE_CUBE_MAP, /*smooth=*/true, /*repeat=*/false, /*mipmaps=*/false);
  cubemap.ReleaseFaces();
}

// A render target's framebuffer: its colour texture (the target's WTexture2D) plus, if asked for,
// a depth renderbuffer (never sampled, so it needn't be a texture).
class GLFramebufferObject : public GpuResource {
 public:
  explicit GLFramebufferObject(bool depth) {
    glGenFramebuffers(1, &framebuffer_);
    if (depth) glGenRenderbuffers(1, &depth_);
  }
  ~GLFramebufferObject() override {
    if (!platform::HasCurrentGLContext()) return;
    glDeleteFramebuffers(1, &framebuffer_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
  }
  GLuint framebuffer_ = 0;
  GLuint depth_ = 0;
  int width_ = 0, height_ = 0;
  TextureFormat format_ = TextureFormat::RGBA8;
};

}  // namespace

GLuint GLTexture(const WTexture& texture) {
  auto* gl = static_cast<GLTextureObject*>(texture.Gpu());
  const bool created = gl == nullptr;
  if (created) {
    auto made = std::make_unique<GLTextureObject>(texture.IsCubemap() ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D);
    gl = made.get();
    texture.SetGpu(std::move(made));
  }
  if (texture.IsCubemap()) {
    if (created) PrepareCubemap(static_cast<const WCubemap&>(texture), *gl);
  } else {
    Prepare2D(static_cast<const WTexture2D&>(texture), *gl, created);
  }
  return gl->id_;
}

void BindGLTexture(const WTexture& texture, unsigned int unit) {
  glActiveTexture(GL_TEXTURE0 + unit);  // first: an upload binds on the active unit
  const GLuint id = GLTexture(texture);
  glBindTexture(texture.IsCubemap() ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, id);
}

const GLShader* GLShaderFor(const Shader& shader) {
  if (!shader.IsValid()) return nullptr;
  if (auto* gl = static_cast<const GLShader*>(shader.Gpu())) return gl;
  const auto start = std::chrono::steady_clock::now();
  auto made = std::make_unique<GLShader>(shader);
  const GLShader* gl = made.get();
  shader.SetGpu(std::move(made));
  if (gl->Program() == 0) {
    shader.MarkFailed();  // the compile error was logged; don't try every frame
    return nullptr;
  }
  if (warmed_up) {  // not prepared by the warm-up: it costs a frame now (worth knowing on phones)
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    WLW_LOG_INFO("Shader compiled during play (%.1f ms): %s", ms, shader.Name().c_str());
  }
  return gl;
}

GLuint GLFramebuffer(RenderTarget& target) {
  auto* gl = static_cast<GLFramebufferObject*>(target.Gpu());
  const bool created = gl == nullptr;
  if (created) {
    if (!CanRenderTo(target.Settings().format)) {
      WLW_LOG_WARN("This GPU can't draw into 16-bit float textures: render target falls back to RGBA8");
      target.SetFormat(TextureFormat::RGBA8);
    }
    auto made = std::make_unique<GLFramebufferObject>(target.Settings().depth);
    gl = made.get();
    target.SetGpu(std::move(made));
  }
  if (!created && gl->width_ == target.Width() && gl->height_ == target.Height() &&
      gl->format_ == target.Settings().format) {
    return gl->framebuffer_;
  }

  // New size (or the first): the colour texture's storage, the depth buffer's, and attach both.
  gl->width_ = target.Width();
  gl->height_ = target.Height();
  gl->format_ = target.Settings().format;
  const GLuint color = GLTexture(*target.GetTexture());
  glBindTexture(GL_TEXTURE_2D, 0);
  if (gl->depth_) {
    glBindRenderbuffer(GL_RENDERBUFFER, gl->depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, gl->width_, gl->height_);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
  }
  GLint previous_framebuffer = 0;  // not always 0: the platform may draw into its own
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  if (gl->depth_) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gl->depth_);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    WLW_LOG_ERROR("Render target %d x %d incomplete (status 0x%x): it will stay empty", gl->width_, gl->height_,
                  status);
  }
  return gl->framebuffer_;
}

bool HasGLExtension(const char* extension) {
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (GLint i = 0; i < count; ++i) {
    const auto* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
    if (name && std::strcmp(name, extension) == 0) return true;
  }
  return false;
}

bool CanRenderTo(TextureFormat format) {
  if (format != TextureFormat::R16F && format != TextureFormat::RGBA16F) return true;
#if defined(WLW_GLES)
  // Asked once: the answer doesn't change.
  static const bool half_float_renderable =
      HasGLExtension("GL_EXT_color_buffer_half_float") || HasGLExtension("GL_EXT_color_buffer_float");
  return half_float_renderable;
#else
  return true;  // desktop GL 3.3: float formats are colour-renderable in core
#endif
}

int PrepareAliveResources() {
  int compiled = 0;
  Resource::ForEachAlive([&compiled](Resource& resource) {
    if (const auto* shader = dynamic_cast<const Shader*>(&resource)) {
      const bool had_program = shader->Gpu() != nullptr;
      if (GLShaderFor(*shader) && !had_program) ++compiled;
    } else if (const auto* texture = dynamic_cast<const WTexture*>(&resource)) {
      glActiveTexture(GL_TEXTURE0);
      GLTexture(*texture);
    }
    // Render targets: made when first drawn into, at the size they have then.
  });
  glBindTexture(GL_TEXTURE_2D, 0);
  warmed_up = true;
  return compiled;
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
