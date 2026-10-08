#ifdef WLW_RENDERER_GL

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "wlw/platform/platform.h"
#include "wlw/rendering/gl.h"
#include "wlw/rendering/instance_buffer.h"

namespace wlw::rendering {

// GL buffer holding a node's per-instance data, fed to the 3D shader's instance attributes
// (locations 4..7 = transform, 8 = tint; see basic_shaders.h).
class GLInstanceBuffer : public WInstanceBuffer {
 public:
  static constexpr GLuint kTransformLocation = 4;  // a mat4 attribute uses 4 vec4 locations
  static constexpr GLuint kTintLocation = 8;

  GLInstanceBuffer() { glGenBuffers(1, &vbo_); }
  ~GLInstanceBuffer() override {
    if (platform::HasCurrentGLContext()) glDeleteBuffers(1, &vbo_);
  }

  // Copies the instances to the GPU, only if they changed since the last upload. The storage
  // grows (doubling) but never shrinks, so steady particle counts don't reallocate.
  void Upload(const std::vector<InstanceData>& instances, uint64_t version) {
    if (version == uploaded_version_) return;
    uploaded_version_ = version;
    const auto bytes = static_cast<GLsizeiptr>(instances.size() * sizeof(InstanceData));
    if (bytes == 0) return;
    if (bytes > capacity_bytes_) capacity_bytes_ = 2 * bytes;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // "Orphaning": give the buffer fresh storage before writing, so we never wait for the GPU
    // to finish drawing last frame's instances out of the old storage.
    glBufferData(GL_ARRAY_BUFFER, capacity_bytes_, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instances.data());
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

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
