#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "wlw/rendering/gl.h"

// Builds GL programs from shader text through the preprocessor (#include, #define, #line), the
// one way every program is made: the engine's shaders, the UI's, games' custom shaders.
namespace wlw::rendering {

// The per-frame uniform block (wlw/frame.glsl) is bound to this binding point in every program
// that uses it; the renderer uploads it once per viewport.
inline constexpr GLuint kFrameBlockBinding = 0;

struct ShaderStageSource {
  std::string_view name;             // for error messages
  std::string_view text;             // the stage's main text
  std::vector<std::string> prelude;  // files included before it (its line 1 stays line 1)
};

// Compiles + links a program; each stage also gets WLW_VERTEX / WLW_FRAGMENT defined. Errors are
// logged with file names and lines (label says which program). Returns 0 on failure.
GLuint BuildProgram(std::string_view label, const ShaderStageSource& vertex, const ShaderStageSource& fragment,
                    const std::vector<std::string>& defines = {});

// The same from two of the engine's shader files (shader_library.h), e.g. ("mesh.vert", "mesh.frag").
GLuint BuildEngineProgram(std::string_view vertex_file, std::string_view fragment_file,
                          const std::vector<std::string>& defines = {});

}  // namespace wlw::rendering
