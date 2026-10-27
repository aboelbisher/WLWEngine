#ifdef WLW_RENDERER_GL

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

#include "wlw/platform/platform.h"
#include "wlw/rendering/gl.h"
#include "wlw/rendering/instance_buffer.h"

namespace wlw::rendering {

// GL buffer holding a node's copies (Instance, 52 bytes each), fed to the 3D shader's instance
// attributes: locations 4..6 = the transform's rows, 7 = the RGBA8 tint (shaders/wlw/instance.glsl).
class GLInstanceBuffer : public WInstanceBuffer {
 public:
  static constexpr GLuint kRowLocation = 4;  // rows 0, 1, 2 at 4, 5, 6
  static constexpr GLuint kTintLocation = 7;

  GLInstanceBuffer() { glGenBuffers(1, &vbo_); }
  ~GLInstanceBuffer() override {
    if (platform::HasCurrentGLContext()) glDeleteBuffers(1, &vbo_);
  }

  // Copies the instances to the GPU, only if they changed since the last upload. The storage
  // grows (doubling) but never shrinks, so steady particle counts don't reallocate. Returns the
  // bytes copied (0 = unchanged).
  size_t Upload(std::span<const Instance> instances, uint64_t version) {
    if (version == uploaded_version_) return 0;
    uploaded_version_ = version;
    const auto bytes = static_cast<GLsizeiptr>(instances.size_bytes());
    if (bytes == 0) return 0;
    if (bytes > capacity_bytes_) capacity_bytes_ = 2 * bytes;
    UploadOrphaned(vbo_, capacity_bytes_, instances.data(), bytes);
    return static_cast<size_t>(bytes);
  }

  // "Orphaning": give the buffer fresh storage before writing, so we never wait for the GPU to
  // finish drawing last frame's data out of the old storage (the driver recycles it later).
  static void UploadOrphaned(GLuint vbo, GLsizeiptr capacity_bytes, const void* data, GLsizeiptr bytes) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, capacity_bytes, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, data);
  }

  // Points the instance attributes at this buffer, advancing once per instance. Call with the
  // mesh's VAO bound, and Detach() after drawing: the VAO is shared with non-instanced draws.
  void Attach() const {
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = sizeof(Instance);
    for (GLuint row = 0; row < 3; ++row) {
      const GLuint location = kRowLocation + row;
      glEnableVertexAttribArray(location);
      glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<void*>(offsetof(Instance, rows) + row * sizeof(glm::vec4)));
      glVertexAttribDivisor(location, 1);
    }
    glEnableVertexAttribArray(kTintLocation);  // 4 bytes, scaled to 0..1 by the GPU
    glVertexAttribPointer(kTintLocation, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(Instance, color)));
    glVertexAttribDivisor(kTintLocation, 1);
  }

  static void Detach() {
    for (GLuint location = kRowLocation; location <= kTintLocation; ++location) {
      glVertexAttribDivisor(location, 0);
      glDisableVertexAttribArray(location);
    }
  }

 private:
  GLuint vbo_ = 0;
  GLsizeiptr capacity_bytes_ = 0;
  uint64_t uploaded_version_ = UINT64_MAX;  // nothing uploaded yet
};

// One per-instance attribute of a GLQuadBuffer: `components` values of `type` at `offset` in each instance.
struct QuadAttribute {
  GLuint location;
  GLint components;
  GLenum type;
  bool normalized;  // integer types read as 0..1
  size_t offset;
};

// Instanced quads with no vertex buffer at all: the quad's 4 corners come from gl_VertexID in the vertex shader, and
// a vertex array holds only per-instance attributes (divisor 1) into one buffer sized for the batch's capacity once.
// Drawn as a triangle strip of 4 vertices, once per instance, in one call. The base of the sprite and billboard
// buffers (GL-only; owned by the node like any WInstanceBuffer).
class GLQuadBuffer : public WInstanceBuffer {
 public:
  GLQuadBuffer(size_t capacity, size_t stride, std::initializer_list<QuadAttribute> attributes)
      : capacity_bytes_(static_cast<GLsizeiptr>(std::max<size_t>(capacity, 1) * stride)) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, capacity_bytes_, nullptr, GL_DYNAMIC_DRAW);
    for (const QuadAttribute& a : attributes) {
      glEnableVertexAttribArray(a.location);
      glVertexAttribPointer(a.location, a.components, a.type, a.normalized ? GL_TRUE : GL_FALSE,
                            static_cast<GLsizei>(stride), reinterpret_cast<void*>(a.offset));
      glVertexAttribDivisor(a.location, 1);
    }
    glBindVertexArray(0);
  }
  ~GLQuadBuffer() override {
    if (!platform::HasCurrentGLContext()) return;
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
  }

  // Copies the instances to the GPU if they changed since the last upload (cut at the capacity). Returns the bytes
  // copied (0 = unchanged).
  size_t UploadBytes(const void* data, size_t size_bytes, uint64_t version) {
    if (version == uploaded_version_) return 0;
    uploaded_version_ = version;
    const auto bytes = static_cast<GLsizeiptr>(std::min(size_bytes, static_cast<size_t>(capacity_bytes_)));
    if (bytes == 0) return 0;
    GLInstanceBuffer::UploadOrphaned(vbo_, capacity_bytes_, data, bytes);
    return static_cast<size_t>(bytes);
  }

  void Draw(GLsizei count) const {
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
    glBindVertexArray(0);
  }

 private:
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLsizeiptr capacity_bytes_;
  uint64_t uploaded_version_ = UINT64_MAX;
};

// A SpriteBatch's dots on the GPU: 16 bytes each (SpriteInstance), read by shaders/sprite.vert at
// locations 4 (x, y, size) and 5 (RGBA8 colour, scaled to 0..1).
class GLSpriteBuffer : public GLQuadBuffer {
 public:
  static constexpr GLuint kSpriteLocation = 4;
  static constexpr GLuint kColorLocation = 5;

  explicit GLSpriteBuffer(size_t capacity)
      : GLQuadBuffer(capacity, sizeof(SpriteInstance),
                     {{kSpriteLocation, 3, GL_FLOAT, false, offsetof(SpriteInstance, position)},  // position.xy, size
                      {kColorLocation, 4, GL_UNSIGNED_BYTE, true, offsetof(SpriteInstance, color)}}) {}

  size_t Upload(std::span<const SpriteInstance> sprites, uint64_t version) {
    return UploadBytes(sprites.data(), sprites.size_bytes(), version);
  }
};

// A BillboardBatch's quads on the GPU: 40 bytes each (BillboardInstance), read by shaders/billboard.vert at
// locations 4 (centre, size), 5 (stretch, rotation), 6 (RGBA8 colour, scaled to 0..1) and 7 (intensity).
class GLBillboardBuffer : public GLQuadBuffer {
 public:
  static constexpr GLuint kCenterLocation = 4;
  static constexpr GLuint kStretchLocation = 5;
  static constexpr GLuint kColorLocation = 6;
  static constexpr GLuint kIntensityLocation = 7;

  explicit GLBillboardBuffer(size_t capacity)
      : GLQuadBuffer(capacity, sizeof(BillboardInstance),
                     {{kCenterLocation, 4, GL_FLOAT, false, offsetof(BillboardInstance, position)},  // + size
                      {kStretchLocation, 4, GL_FLOAT, false, offsetof(BillboardInstance, stretch)},  // + rotation
                      {kColorLocation, 4, GL_UNSIGNED_BYTE, true, offsetof(BillboardInstance, color)},
                      {kIntensityLocation, 1, GL_FLOAT, false, offsetof(BillboardInstance, intensity)}}) {}

  size_t Upload(std::span<const BillboardInstance> billboards, uint64_t version) {
    return UploadBytes(billboards.data(), billboards.size_bytes(), version);
  }
};

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
