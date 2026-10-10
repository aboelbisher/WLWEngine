#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl_shader.h"

#include <type_traits>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace wlw::rendering {

namespace {

// ---- What the engine puts in front of a game's source (documented in shader.h) ----

// Canvas: the UI's vertex layout (see ui::UIVertex); the rectangle's uv is 0..1.
constexpr const char* kCanvasVertex = R"(
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
void main() {
  gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
  vUV = aUV;
  vColor = aColor;
}
)";

constexpr const char* kCanvasFragmentInputs = R"(
in vec2 vUV;
in vec4 vColor;
uniform vec2 uSize;
out vec4 FragColor;
)";

// Mesh: the 3D vertex layout (core::Vertex3D, instances: see gl_instance_buffer.h).
constexpr const char* kMeshVertexInputs = R"(
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;
layout (location = 4) in mat4 aInstanceTransform;
layout (location = 8) in vec4 aInstanceTint;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCameraPos;
uniform bool uInstanced;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
)";

constexpr const char* kMeshDefaultVertex = R"(
void main() {
  mat4 world = uInstanced ? uModel * aInstanceTransform : uModel;
  vec4 world_pos = world * vec4(aPos, 1.0);
  gl_Position = uProjection * uView * world_pos;
  vWorldPos = world_pos.xyz;
  vNormal = mat3(transpose(inverse(world))) * aNormal;
  vUV = aUV;
  vColor = uInstanced ? aColor * aInstanceTint : aColor;
}
)";

constexpr const char* kMeshFragmentInputs = R"(
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCameraPos;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;
)";

GLuint Compile(GLenum type, const std::string& body) {
  const GLuint shader = glCreateShader(type);
  const char* sources[] = {kGLSLHeader, body.c_str()};
  glShaderSource(shader, 2, sources, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> message(length > 1 ? length : 1, '\0');
    glGetShaderInfoLog(shader, static_cast<GLsizei>(message.size()), nullptr, message.data());
    // Line numbers are the game's text's (GLShader puts #line 1 before it).
    WLW_LOG_ERROR("Custom %s shader failed to compile:\n%s", type == GL_VERTEX_SHADER ? "vertex" : "fragment",
                  message.data());
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

GLShader::GLShader(const std::string& code) {
  const ShaderSource source = SplitShaderSource(code);
  if (!source.ok) {
    WLW_LOG_ERROR("Custom shader: %s", source.error.c_str());
    return;
  }
  target_ = source.target;
  // The engine's inputs, then the game's text from its line 1 (so error lines match the text).
  const std::string line_one = "\n#line 1\n";
  std::string vertex_source;
  std::string fragment_source;
  if (target_ == Target::Canvas) {
    vertex_source = kCanvasVertex;
    fragment_source = kCanvasFragmentInputs + line_one + source.fragment;
  } else {
    vertex_source = source.vertex.empty() ? std::string(kMeshVertexInputs) + kMeshDefaultVertex
                                          : kMeshVertexInputs + line_one + source.vertex;
    fragment_source = kMeshFragmentInputs + line_one + source.fragment;
  }

  const GLuint vs = Compile(GL_VERTEX_SHADER, vertex_source);
  const GLuint fs = Compile(GL_FRAGMENT_SHADER, fragment_source);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint linked = GL_FALSE;
  glGetProgramiv(program_, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    char message[1024] = {};
    glGetProgramInfoLog(program_, sizeof(message), nullptr, message);
    WLW_LOG_ERROR("Custom shader failed to link:\n%s", message);
    glDeleteProgram(program_);
    program_ = 0;
    return;
  }
  valid_ = true;
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
