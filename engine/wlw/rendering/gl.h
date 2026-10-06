#pragma once

// One renderer, two GL flavours:
//   desktop (Windows/macOS/Linux): OpenGL 3.3 core, functions loaded at runtime by glad
//   mobile  (Android, later iOS):  OpenGL ES 3.0, functions linked directly from the system
// The APIs overlap almost entirely; the shader language differs mainly in its first line, so
// shader sources are written without a #version and kGLSLHeader is prepended at compile time.

#if defined(WLW_GLES)
#include <GLES3/gl3.h>
namespace wlw::rendering {
inline constexpr const char* kGLSLHeader =
    "#version 300 es\n"
    "precision highp float;\n"  // GLSL ES has no default float precision in fragment shaders
    "precision highp int;\n";
}  // namespace wlw::rendering
#else
#include <glad/glad.h>
namespace wlw::rendering {
inline constexpr const char* kGLSLHeader = "#version 330 core\n";
}  // namespace wlw::rendering
#endif
