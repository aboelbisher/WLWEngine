#pragma once

#include "wlw/rendering/blend_mode.h"
#include "wlw/rendering/gl.h"
#include "wlw/rendering/gl_blend.h"

namespace wlw::rendering {

// Remembers the GL state the 3D renderer sets (program, blend mode, depth writes, the 2D texture
// on unit 0) and skips calls that change nothing, like the big engines' state filtering (Unreal's
// RHI, Unity's GfxDevice). Others draw too (the UI renderer, the debug UI), so Reset() before
// relying on it: the next call of each kind then always goes to GL.
class GLStateCache {
 public:
  void Reset() {
    program_ = kUnknown;
    blend_known_ = false;
    depth_write_known_ = false;
    texture0_ = kUnknown;
  }

  void UseProgram(GLuint program) {
    if (program == program_) {
      ++skipped_;
      return;
    }
    glUseProgram(program);
    program_ = program;
    ++program_changes_;
  }

  void SetBlendMode(BlendMode blend) {
    if (blend_known_ && blend == blend_) {
      ++skipped_;
      return;
    }
    ApplyBlendMode(blend);
    blend_ = blend;
    blend_known_ = true;
  }

  void SetDepthWrite(bool write) {
    if (depth_write_known_ && write == depth_write_) {
      ++skipped_;
      return;
    }
    glDepthMask(write ? GL_TRUE : GL_FALSE);
    depth_write_ = write;
    depth_write_known_ = true;
  }

  // Binds a 2D texture to unit 0 (leaves unit 0 active).
  void BindTexture0(GLuint texture) {
    if (texture == texture0_) {
      ++skipped_;
      return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    texture0_ = texture;
    ++texture_binds_;
  }
  // Something bound a texture to unit 0 behind the cache's back.
  void ForgetTexture0() { texture0_ = kUnknown; }

  // Counters for the debug panel (since the last ResetCounters).
  int ProgramChanges() const { return program_changes_; }
  int TextureBinds() const { return texture_binds_; }
  int Skipped() const { return skipped_; }
  void ResetCounters() { program_changes_ = texture_binds_ = skipped_ = 0; }

 private:
  static constexpr GLuint kUnknown = 0xFFFFFFFFu;  // no GL name is this
  GLuint program_ = kUnknown;
  BlendMode blend_ = BlendMode::Opaque;
  bool blend_known_ = false;
  bool depth_write_ = true;
  bool depth_write_known_ = false;
  GLuint texture0_ = kUnknown;
  int program_changes_ = 0;
  int texture_binds_ = 0;
  int skipped_ = 0;
};

}  // namespace wlw::rendering
