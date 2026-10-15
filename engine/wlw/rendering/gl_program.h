#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/rendering/gl.h"
#include "wlw/rendering/glsl_header.h"

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

// The shader language of the current context: GLSL ES 3.00 on GLES (phones, or a desktop OpenGL ES
// context), else GLSL 3.30.
GlslDialect CurrentGlslDialect();

// What mediump floats are in fragment shaders that opt in, on this GPU (glGetShaderPrecisionFormat):
// "16-bit", "32-bit: no gain here", or "ignored (desktop GL)"; ", opt-ins off (r.mediump 0)" when turned off.
std::string DescribeFragmentPrecision();

// Sees every stage's final text (header included) just before it compiles: for tools and tests
// (wlw_gpu_tests --dump-shaders feeds them to the Mali Offline Compiler). Empty = none.
using ShaderSourceObserver = std::function<void(std::string_view label, ShaderStage stage, const std::string& text)>;
void SetShaderSourceObserver(ShaderSourceObserver observer);

}  // namespace wlw::rendering
