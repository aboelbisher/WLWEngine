#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl_shader.h"

#include <type_traits>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl_program.h"
#include "wlw/rendering/shader_library.h"

namespace wlw::rendering {

GLShader::GLShader(const std::string& code) {
  const ShaderSource source = SplitShaderSource(code);
  if (!source.ok) {
    WLW_LOG_ERROR("Custom shader: %s", source.error.c_str());
    return;
  }
  target_ = source.target;
  // The engine's inputs (shaders/custom/*) go in front of each stage of the game's text, which
  // keeps its own line numbers in errors. The game's text may #include shader files too.
  const std::string* stage_file = nullptr;
  if (target_ == Target::Canvas) {
    stage_file = FindShaderFile("custom/canvas.vert");
    if (!stage_file) return;
    program_ = BuildProgram("custom canvas shader", {"custom/canvas.vert", *stage_file, {}},
                            {"shader", source.fragment, {"custom/canvas_fragment.glsl"}});
  } else if (source.vertex.empty()) {
    stage_file = FindShaderFile("custom/mesh_default.vert");
    if (!stage_file) return;
    program_ = BuildProgram("custom mesh shader", {"custom/mesh_default.vert", *stage_file, {"custom/mesh_vertex.glsl"}},
                            {"shader", source.fragment, {"custom/mesh_fragment.glsl"}});
  } else {
    program_ = BuildProgram("custom mesh shader", {"shader", source.vertex, {"custom/mesh_vertex.glsl"}},
                            {"shader", source.fragment, {"custom/mesh_fragment.glsl"}});
  }
  valid_ = program_ != 0;
}

GLShader::~GLShader() {
  if (program_ && platform::HasCurrentGLContext()) glDeleteProgram(program_);
}

GLint GLShader::Location(const std::string& name) const {
  auto [it, inserted] = locations_.try_emplace(name, -1);
  if (inserted) it->second = glGetUniformLocation(program_, name.c_str());
  return it->second;
}

int GLShader::ApplyParams(const ShaderMaterial& material, int first_unit) const {
  int unit = first_unit;
  for (const auto& [name, param] : material.Params()) {
    const GLint location = Location(name);
    if (location < 0) continue;  // not in this shader (or optimized away: unused)
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, float>) {
            glUniform1f(location, value);
          } else if constexpr (std::is_same_v<T, glm::vec2>) {
            glUniform2fv(location, 1, glm::value_ptr(value));
          } else if constexpr (std::is_same_v<T, glm::vec3>) {
            glUniform3fv(location, 1, glm::value_ptr(value));
          } else if constexpr (std::is_same_v<T, glm::vec4>) {
            glUniform4fv(location, 1, glm::value_ptr(value));
          } else if constexpr (std::is_same_v<T, glm::mat4>) {
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
          } else if (value) {  // a texture
            value->Bind(static_cast<unsigned int>(unit));
            glUniform1i(location, unit);
            ++unit;
          }
        },
        param);
  }
  glActiveTexture(GL_TEXTURE0);
  return unit;
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
