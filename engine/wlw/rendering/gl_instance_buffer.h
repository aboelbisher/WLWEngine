#ifdef WLW_RENDERER_GL

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wlw/platform/platform.h"
#include "wlw/rendering/gl.h"
#include "wlw/rendering/instance_buffer.h"

namespace wlw::rendering {

// GL buffer holding a node's per-instance data, fed to the 3D shader's instance attributes
// (locations 4..7 = transform, 8 = tint; see shaders/mesh.vert).
class GLInstanceBuffer : public WInstanceBuffer {
 public:
  static constexpr GLuint kTransformLocation = 4;  // a mat4 attribute uses 4 vec4 locations
  static constexpr GLuint kTintLocation = 8;

  GLInstanceBuffer() { glGenBuffers(1, &vbo_); }
  ~GLInstanceBuffer() override {
    if (platform::HasCurrentGLContext()) glDeleteBuffers(1, &vbo_);
  }

  // Copies the instances to the GPU, only if they changed since the last upload. The storage
  // grows (doubling) but never shrinks, so steady particle counts don't reallocate. Returns the
  // bytes copied (0 = unchanged).
  size_t Upload(const std::vector<InstanceData>& instances, uint64_t version) {
    if (version == uploaded_version_) return 0;
    uploaded_version_ = version;
    const auto bytes = static_cast<GLsizeiptr>(instances.size() * sizeof(InstanceData));
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
    const GLsizei stride = sizeof(InstanceData);
    for (GLuint column = 0; column < 4; ++column) {
      const GLuint location = kTransformLocation + column;
      glEnableVertexAttribArray(location);
      glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride,
                            reinterpret_cast<void*>(offsetof(InstanceData, transform) + column * 4 * sizeof(float)));
      glVertexAttribDivisor(location, 1);
    }
    glEnableVertexAttribArray(kTintLocation);
    glVertexAttribPointer(kTintLocation, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(InstanceData, tint)));
    glVertexAttribDivisor(kTintLocation, 1);
  }

  static void Detach() {
    for (GLuint location = kTransformLocation; location <= kTintLocation; ++location) {
      glVertexAttribDivisor(location, 0);
      glDisableVertexAttribArray(location);
    }
  }

 private:
  GLuint vbo_ = 0;
  GLsizeiptr capacity_bytes_ = 0;
  uint64_t uploaded_version_ = UINT64_MAX;  // nothing uploaded yet
};

// A SpriteBatch's dots on the GPU: 16 bytes each (SpriteInstance), read by shaders/sprite.vert at
// locations 4 (x, y, size) and 5 (RGBA8 colour, scaled to 0..1). It has its own vertex array with
// only these per-instance attributes: the quad's 4 corners come from gl_VertexID, so there is no
// vertex buffer at all. Drawn as a triangle strip of 4 vertices, once per dot, in one call.
class GLSpriteBuffer : public WInstanceBuffer {
 public:
  static constexpr GLuint kSpriteLocation = 4;
  static constexpr GLuint kColorLocation = 5;

  explicit GLSpriteBuffer(size_t capacity)
      : capacity_bytes_(static_cast<GLsizeiptr>(std::max<size_t>(capacity, 1) * sizeof(SpriteInstance))) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, capacity_bytes_, nullptr, GL_DYNAMIC_DRAW);
    const GLsizei stride = sizeof(SpriteInstance);
    glEnableVertexAttribArray(kSpriteLocation);
    glVertexAttribPointer(kSpriteLocation, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(SpriteInstance, position)));  // position.xy, size
    glVertexAttribDivisor(kSpriteLocation, 1);
    glEnableVertexAttribArray(kColorLocation);
    glVertexAttribPointer(kColorLocation, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<void*>(offsetof(SpriteInstance, color)));
    glVertexAttribDivisor(kColorLocation, 1);
    glBindVertexArray(0);
  }
  ~GLSpriteBuffer() override {
    if (!platform::HasCurrentGLContext()) return;
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
  }

  // Copies the dots to the GPU if they changed since the last upload (the storage was sized for
  // the batch's capacity once). Returns the bytes copied (0 = unchanged).
  size_t Upload(std::span<const SpriteInstance> sprites, uint64_t version) {
    if (version == uploaded_version_) return 0;
    uploaded_version_ = version;
    const auto bytes = static_cast<GLsizeiptr>(std::min(sprites.size_bytes(), static_cast<size_t>(capacity_bytes_)));
    if (bytes == 0) return 0;
    GLInstanceBuffer::UploadOrphaned(vbo_, capacity_bytes_, sprites.data(), bytes);
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

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
