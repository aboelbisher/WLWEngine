#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl_shader.h"

#include <type_traits>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl_program.h"
#include "wlw/rendering/gl_resources.h"
#include "wlw/rendering/shader_library.h"

namespace wlw::rendering {

namespace {

bool IsInstanceUniform(const std::vector<InstanceUniform>& uniforms, const std::string& name) {
  for (const InstanceUniform& uniform : uniforms) {
    if (uniform.name == name) return true;
  }
  return false;
}

// The material's value for an instance uniform as a vec4; 0 if it has none (or one that isn't a float or vector).
glm::vec4 MaterialValue(const ShaderMaterial& material, const std::string& name) {
  const ShaderMaterial::Param* param = material.GetParam(name);
  if (!param) return glm::vec4(0.0f);
  return std::visit(
      [](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                      std::is_same_v<T, glm::vec4>) {
          return InstanceParamVec4(InstanceParam(value));
        } else {
          return glm::vec4(0.0f);
        }
      },
      *param);
}

void Upload(GLint location, InstanceUniformType type, const glm::vec4& value) {
  switch (type) {
    case InstanceUniformType::Float: glUniform1f(location, value.x); break;
    case InstanceUniformType::Vec2: glUniform2fv(location, 1, glm::value_ptr(value)); break;
    case InstanceUniformType::Vec3: glUniform3fv(location, 1, glm::value_ptr(value)); break;
    case InstanceUniformType::Vec4: glUniform4fv(location, 1, glm::value_ptr(value)); break;
  }
}

}  // namespace

GLShader::GLShader(const Shader& shader) {
  const ShaderSource& source = shader.Internal().Source();
  if (!source.ok) return;  // the text's error was logged when the Shader was made
  const std::string label = shader.Name();
  // The engine's inputs (shaders/custom/*) go in front of each stage of the game's text, which
  // keeps its own line numbers in errors. The game's text may #include shader files too.
  const std::string* stage_file = nullptr;
  if (source.target == ShaderTarget::Canvas) {
    stage_file = FindShaderFile("custom/canvas.vert");
    if (!stage_file) return;
    program_ = BuildProgram(label, {"custom/canvas.vert", *stage_file, {}},
                            {label, source.fragment, {"custom/canvas_fragment.glsl"}});
  } else if (source.target == ShaderTarget::Post) {
    stage_file = FindShaderFile("custom/post.vert");
    if (!stage_file) return;
    program_ = BuildProgram(label, {"custom/post.vert", *stage_file, {}},
                            {label, source.fragment, {"custom/post_fragment.glsl"}});
  } else if (source.vertex.empty()) {
    stage_file = FindShaderFile("custom/mesh_default.vert");
    if (!stage_file) return;
    program_ = BuildProgram(label, {"custom/mesh_default.vert", *stage_file, {"custom/mesh_vertex.glsl"}},
                            {label, source.fragment, {"custom/mesh_fragment.glsl"}});
  } else {
    program_ = BuildProgram(label, {label, source.vertex, {"custom/mesh_vertex.glsl"}},
                            {label, source.fragment, {"custom/mesh_fragment.glsl"}});
  }
  if (program_ == 0) return;

  inputs_.model = Location("uModel");
  inputs_.normal_matrix = Location("uNormalMatrix");
  inputs_.instanced = Location("uInstanced");
  inputs_.receives_shadow = Location("uReceivesShadow");
  inputs_.shadow_map = Location("uShadowMap");
  inputs_.projection = Location("uProjection");
  inputs_.size = Location("uSize");
  inputs_.source = Location("uSource");
  inputs_.scene = Location("uScene");
  inputs_.texel = Location("uTexel");
  inputs_.output_size = Location("uOutputSize");
  for (const InstanceUniform& uniform : source.instance_uniforms) {
    instance_slots_.push_back({uniform.name, uniform.type, Location(uniform.name)});
  }
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
  const uint64_t id = material.Internal().Id();
  const uint64_t version = material.Internal().Version();
  if (id == material_id_ && version == material_version_ && first_unit == material_unit_) {
    // The program has these values: only the textures, whose units other draws may have rebound.
    int unit = first_unit;
    for (const WTexture* texture : material_textures_) BindGLTexture(*texture, static_cast<unsigned int>(unit++));
    glActiveTexture(GL_TEXTURE0);
    return unit;
  }
  material_id_ = id;
  material_version_ = version;
  material_unit_ = first_unit;
  material_textures_.clear();

  const std::vector<InstanceUniform>& instance_uniforms = material.GetShader()->InstanceUniforms();
  int unit = first_unit;
  for (const auto& [name, param] : material.Params()) {
    const GLint location = Location(name);
    if (location < 0) continue;                                // not in this shader (or optimized away: unused)
    if (IsInstanceUniform(instance_uniforms, name)) continue;  // per draw: ApplyInstanceParams
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
            BindGLTexture(*value, static_cast<unsigned int>(unit));
            glUniform1i(location, unit);
            material_textures_.push_back(value.get());
            ++unit;
          }
        },
        param);
  }
  for (InstanceSlot& slot : instance_slots_) slot.fallback = MaterialValue(material, slot.name);
  glActiveTexture(GL_TEXTURE0);
  return unit;
}

void GLShader::ApplyInstanceParams(const InstanceParams* node) const {
  for (InstanceSlot& slot : instance_slots_) {
    if (slot.location < 0) continue;
    glm::vec4 value = slot.fallback;
    if (node) {
      for (const auto& [name, param] : *node) {
        if (name == slot.name) {
          value = InstanceParamVec4(param);
          break;
        }
      }
    }
    if (value == slot.uploaded) continue;
    Upload(slot.location, slot.type, value);
    slot.uploaded = value;
  }
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
