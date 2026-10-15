#pragma once

// One renderer, two GL flavours:
//   desktop (Windows/macOS/Linux): OpenGL 3.3 core, functions loaded at runtime by glad
//   mobile  (Android, later iOS):  OpenGL ES 3.0, functions linked directly from the system
// The APIs overlap almost entirely; the shader language differs mainly in its first lines, so
// shader sources are written without a #version and GlslHeader (glsl_header.h) is prepended at
// compile time. (A desktop build can also make an OpenGL ES context, through glad: tests/gpu
// compiles every shader as GLSL ES that way.)

#if defined(WLW_GLES)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif
