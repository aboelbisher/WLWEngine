// Empty without WLW_DEBUG_DRAW (release builds): the renderer only draws debug shapes with it.
#if defined(WLW_RENDERER_GL) && WLW_DEBUG_DRAW

#include "wlw/rendering/gl_debug_draw.h"

#include <cstddef>

#include "wlw/core/profile.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl_instance_buffer.h"
#include "wlw/rendering/gl_program.h"

namespace wlw::rendering {

namespace {

// Per-line attributes (debug_line.vert): locations 4..7, one step per instance.
constexpr GLuint kStart = 4, kEnd = 5, kColor = 6, kWidth = 7;
// Per-corner attributes (debug_triangle.vert).
constexpr GLuint kPosition = 0, kCornerColor = 1;

// Points the line attributes at the lines from `first` on (GLES 3.0 has no base instance: the offset does it).
void PointLineAttributes(uint32_t first) {
  const GLsizei stride = sizeof(debug::DebugLine);
  const size_t base = static_cast<size_t>(first) * sizeof(debug::DebugLine);
  glVertexAttribPointer(kStart, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(base + offsetof(debug::DebugLine, a)));
  glVertexAttribPointer(kEnd, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(base + offsetof(debug::DebugLine, b)));
  glVertexAttribPointer(kColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                        reinterpret_cast<void*>(base + offsetof(debug::DebugLine, color)));
  glVertexAttribPointer(kWidth, 1, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<void*>(base + offsetof(debug::DebugLine, width)));
}

// Copies `bytes` into the buffer, growing it (doubling) when they don't fit; orphaned like the instance buffers.
void UploadGrowing(GLuint vbo, GLsizeiptr& capacity, const void* data, GLsizeiptr bytes) {
  if (bytes > capacity) capacity = 2 * bytes;
  GLInstanceBuffer::UploadOrphaned(vbo, capacity, data, bytes);
}

}  // namespace

GLDebugDraw::~GLDebugDraw() {
  if (!platform::HasCurrentGLContext()) return;
  for (const Program* program : {&lines_, &triangles_}) {
    if (program->id) glDeleteProgram(program->id);
  }
  const GLuint buffers[] = {line_vbo_, triangle_vbo_};
  const GLuint arrays[] = {line_vao_, triangle_vao_};
  glDeleteBuffers(2, buffers);  // 0s are ignored
  glDeleteVertexArrays(2, arrays);
}

bool GLDebugDraw::Prepare() {
  if (failed_) return false;
  if (lines_.id != 0) return true;
  lines_.id = BuildEngineProgram("debug_line.vert", "debug_line.frag");
  triangles_.id = BuildEngineProgram("debug_triangle.vert", "debug_triangle.frag");
  if (lines_.id == 0 || triangles_.id == 0) {
    failed_ = true;  // logged by BuildEngineProgram; not retried every frame
    return false;
  }
  for (Program* program : {&lines_, &triangles_}) {
    program->alpha = glGetUniformLocation(program->id, "uAlpha");
    program->depth_bias = glGetUniformLocation(program->id, "uDepthBias");
  }
  lines_.viewport_pixels = glGetUniformLocation(lines_.id, "uViewportPixels");
  lines_.width_scale = glGetUniformLocation(lines_.id, "uWidthScale");

  // Lines: no vertex buffer (the corners come from gl_VertexID), only per-instance attributes.
  glGenVertexArrays(1, &line_vao_);
  glGenBuffers(1, &line_vbo_);
  glBindVertexArray(line_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
  for (GLuint location : {kStart, kEnd, kColor, kWidth}) {
    glEnableVertexAttribArray(location);
    glVertexAttribDivisor(location, 1);
  }
  PointLineAttributes(0);
  // Triangles: one corner per vertex.
  glGenVertexArrays(1, &triangle_vao_);
  glGenBuffers(1, &triangle_vbo_);
  glBindVertexArray(triangle_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, triangle_vbo_);
  glEnableVertexAttribArray(kPosition);
  glVertexAttribPointer(kPosition, 3, GL_FLOAT, GL_FALSE, sizeof(debug::DebugVertex),
                        reinterpret_cast<void*>(offsetof(debug::DebugVertex, position)));
  glEnableVertexAttribArray(kCornerColor);
  glVertexAttribPointer(kCornerColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(debug::DebugVertex),
                        reinterpret_cast<void*>(offsetof(debug::DebugVertex, color)));
  glBindVertexArray(0);
  return true;
}

void GLDebugDraw::Upload(debug::DebugDraw& draw, scene::RenderStats& stats) {
  const auto& lists = draw.Internal().Lists();
  if (&draw == uploaded_from_ && lists.version == uploaded_version_) return;
  uploaded_from_ = &draw;
  uploaded_version_ = lists.version;
  const auto line_bytes = static_cast<GLsizeiptr>(lists.lines.size() * sizeof(debug::DebugLine));
  const auto vertex_bytes = static_cast<GLsizeiptr>(lists.vertices.size() * sizeof(debug::DebugVertex));
  if (line_bytes > 0) UploadGrowing(line_vbo_, line_capacity_, lists.lines.data(), line_bytes);
  if (vertex_bytes > 0) UploadGrowing(triangle_vbo_, triangle_capacity_, lists.vertices.data(), vertex_bytes);
  stats.instance_bytes += static_cast<int>(line_bytes + vertex_bytes);
}

void GLDebugDraw::DrawTriangles(uint32_t first, uint32_t count, float alpha, scene::RenderStats& stats) {
  if (count == 0) return;
  state_->UseProgram(triangles_.id);
  glUniform1f(triangles_.alpha, alpha);
  glBindVertexArray(triangle_vao_);
  glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first * 3), static_cast<GLsizei>(count * 3));
  glBindVertexArray(0);
  ++stats.draw_calls;
  ++stats.debug_draw_calls;
}

void GLDebugDraw::DrawLines(uint32_t first, uint32_t count, float alpha, scene::RenderStats& stats) {
  if (count == 0) return;
  state_->UseProgram(lines_.id);
  glUniform1f(lines_.alpha, alpha);
  glBindVertexArray(line_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
  PointLineAttributes(first);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count));
  glBindVertexArray(0);
  ++stats.draw_calls;
  ++stats.debug_draw_calls;
  stats.instances += static_cast<int>(count);
}

void GLDebugDraw::Draw(debug::DebugDraw& draw, const Target& target, GLStateCache& state, scene::RenderStats& stats) {
  if (!draw.Enabled()) return;
  const auto& lists = draw.Internal().Lists();
  if (lists.lines.empty() && lists.vertices.empty()) return;
  if (!Prepare()) return;
  WLW_PROFILE_SCOPE("Debug draw");
  state_ = &state;
  Upload(draw, stats);

  // The uniforms that are the same for every pass.
  state.UseProgram(lines_.id);
  glUniform2f(lines_.viewport_pixels, target.pixels_x, target.pixels_y);
  glUniform1f(lines_.width_scale, target.width_scale);
  glUniform1f(lines_.depth_bias, target.has_depth ? kDepthBias : 0.0f);
  state.UseProgram(triangles_.id);
  glUniform1f(triangles_.depth_bias, target.has_depth ? kDepthBias : 0.0f);
  state.SetBlendMode(BlendMode::Alpha);
  state.SetDepthWrite(false);

  // A range of depth modes [from, to), triangles then lines.
  auto pass = [&](debug::DepthMode from, debug::DepthMode to, float alpha) {
    const auto f = static_cast<size_t>(from), t = static_cast<size_t>(to);
    DrawTriangles(lists.triangle_starts[f], lists.triangle_starts[t] - lists.triangle_starts[f], alpha, stats);
    DrawLines(lists.line_starts[f], lists.line_starts[t] - lists.line_starts[f], alpha, stats);
  };
  using debug::DepthMode;
  if (target.has_depth) {
    glDepthFunc(GL_GREATER);  // behind what's there: X-ray shapes' hidden parts, faint
    pass(DepthMode::XRay, DepthMode::OnTop, kHiddenAlpha);
    glDepthFunc(GL_LEQUAL);  // in front: tested and X-ray shapes
    pass(DepthMode::Tested, DepthMode::OnTop, 1.0f);
    glDisable(GL_DEPTH_TEST);
    pass(DepthMode::OnTop, static_cast<DepthMode>(debug::kDepthModes), 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
  } else {
    glDisable(GL_DEPTH_TEST);  // nothing to test against: all of it on top
    pass(DepthMode::Tested, static_cast<DepthMode>(debug::kDepthModes), 1.0f);
    glEnable(GL_DEPTH_TEST);
  }
  state.SetBlendMode(BlendMode::Opaque);
  state.SetDepthWrite(true);
  state_ = nullptr;
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL && WLW_DEBUG_DRAW
