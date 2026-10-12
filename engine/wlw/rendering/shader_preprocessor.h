#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/rendering/shader_library.h"

// Turns a shader's text into what the GPU compiles: the variant's #defines first, then the text
// with every #include "file" replaced by that file's text (each file once; nested includes work),
// with #line markers so error messages can name the file and line. No GPU needed (tested on the
// CPU). The #version header is added by the GL backend, in front of all this.
namespace wlw::rendering {

struct PreprocessedShader {
  bool ok = false;
  std::string error;                // why not: a missing file, an include cycle...
  std::string code;                 // ready to compile (without #version)
  std::vector<std::string> files;   // #line source-string numbers -> file names (0 = the main text)
};

using ShaderFileLookup = std::function<const std::string*(std::string_view name)>;

// `name`: what errors call the main text. `prelude`: files included before it (e.g. the inputs a
// game's shader gets); the main text still starts at its own line 1. `defines`: "NAME" or
// "NAME value", one #define each.
PreprocessedShader PreprocessShader(std::string_view name, std::string_view source,
                                    const std::vector<std::string>& defines = {},
                                    const std::vector<std::string>& prelude = {},
                                    const ShaderFileLookup& lookup = FindShaderFile);

// A compiler's log with source-string numbers replaced by file names: "ERROR: 2:14: ..." (GLES,
// ANGLE, Mali, Adreno) and "2(14) : error ..." (NVIDIA, Intel) become "wlw/lighting.glsl:14".
std::string MapShaderLog(std::string_view log, const std::vector<std::string>& files);

}  // namespace wlw::rendering
