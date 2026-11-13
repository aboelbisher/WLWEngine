#ifdef WLW_RENDERER_GL

#pragma once

#include <cstdint>

#include "wlw/debug/debug_draw.h"
#include "wlw/rendering/gl.h"
#include "wlw/rendering/gl_state_cache.h"
#include "wlw/scene/viewport.h"

namespace wlw::rendering {

// Draws an app's debug shapes (debug/debug_draw.h) into the viewport being drawn, right after its main pass: the
// frame block and the depth buffer are its. GL-only, owned by the renderer; its GL objects are made on first use
// and freed by the destructor (with a current context).
class GLDebugDraw {
 public:
  GLDebugDraw() = default;
  ~GLDebugDraw();
  GLDebugDraw(const GLDebugDraw&) = delete;
  GLDebugDraw& operator=(const GLDebugDraw&) = delete;

  // Compiles the two programs and makes the buffers (the renderer's warm-up calls it). False if a program failed
  // (logged once; nothing is drawn after that).
  bool Prepare();

  struct Target {
    float width_scale = 1.0f;  // pixels per dp at the size drawn into
    float pixels_x = 0.0f;     // the size drawn into
    float pixels_y = 0.0f;
    bool has_depth = true;  // without a depth buffer every shape is drawn on top
  };
  // Lines and triangles, in three passes: the hidden parts of X-ray shapes (faint), the visible parts of tested and
  // X-ray ones, then the on-top ones; triangles before lines in each. Leaves blending Opaque, depth writes on and the
  // depth test LESS (the main pass's).
  void Draw(debug::DebugDraw& draw, const Target& target, GLStateCache& state, scene::RenderStats& stats);

  static constexpr float kDepthBias = 0.002f;   // the fraction of its distance a shape moves towards the camera
  static constexpr float kHiddenAlpha = 0.25f;  // X-ray: how strong the hidden parts are

 private:
  struct Program {
    GLuint id = 0;
    GLint alpha = -1;
    GLint depth_bias = -1;
    GLint viewport_pixels = -1;  // lines only
    GLint width_scale = -1;      // lines only
  };
  void Upload(debug::DebugDraw& draw, scene::RenderStats& stats);
  // Draws triangles [first, first + count) and lines [first, first + count) of the uploaded lists.
  void DrawTriangles(uint32_t first, uint32_t count, float alpha, scene::RenderStats& stats);
  void DrawLines(uint32_t first, uint32_t count, float alpha, scene::RenderStats& stats);

  Program lines_;
  Program triangles_;
  bool failed_ = false;
  GLStateCache* state_ = nullptr;  // during Draw
  GLuint line_vao_ = 0;
  GLuint line_vbo_ = 0;
  GLsizeiptr line_capacity_ = 0;  // bytes
  GLuint triangle_vao_ = 0;
  GLuint triangle_vbo_ = 0;
  GLsizeiptr triangle_capacity_ = 0;
  // What the buffers hold: one app's lists at one version (the Studio draws two apps a frame: its own and a game's).
  const debug::DebugDraw* uploaded_from_ = nullptr;
  uint64_t uploaded_version_ = UINT64_MAX;
};

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
