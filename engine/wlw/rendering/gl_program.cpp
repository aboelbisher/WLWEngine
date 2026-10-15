#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl_program.h"

#include <utility>

#include "wlw/core/log.h"
#include "wlw/core/profile.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/glsl_header.h"
#include "wlw/rendering/shader_library.h"
#include "wlw/rendering/shader_preprocessor.h"

namespace wlw::rendering {

namespace {

ShaderSourceObserver g_observer;

std::string Joined(const std::vector<std::string>& defines) {
  std::string text;
  for (const std::string& define : defines) text += (text.empty() ? "" : " ") + define;
  return text.empty() ? "no defines" : text;
}

GLuint CompileStage(GLenum type, std::string_view label, const ShaderStageSource& stage,
                    const std::vector<std::string>& defines) {
  std::vector<std::string> stage_defines = defines;
  stage_defines.emplace_back(type == GL_VERTEX_SHADER ? "WLW_VERTEX" : "WLW_FRAGMENT");
  const PreprocessedShader source = PreprocessShader(stage.name, stage.text, stage_defines, stage.prelude);
  if (!source.ok) {
    WLW_LOG(LogShader, Error, "Shader %.*s: %s", static_cast<int>(label.size()), label.data(), source.error.c_str());
    return 0;
  }
  const ShaderStage shader_stage = type == GL_VERTEX_SHADER ? ShaderStage::Vertex : ShaderStage::Fragment;
  const std::string header = GlslHeader(CurrentGlslDialect(), shader_stage);
  const std::string code = ApplyMediumpSetting(source.code, cv_mediump.Get());  // r.mediump 0: opt-ins off
  if (g_observer) g_observer(label, shader_stage, header + code);
  const GLuint shader = glCreateShader(type);
  const char* parts[] = {header.c_str(), code.c_str()};
  glShaderSource(shader, 2, parts, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(length > 1 ? static_cast<size_t>(length) : 1, '\0');
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    WLW_LOG(LogShader, Error, "Shader %.*s (%s), %s stage failed to compile:\n%s", static_cast<int>(label.size()),
            label.data(), Joined(defines).c_str(), type == GL_VERTEX_SHADER ? "vertex" : "fragment",
            MapShaderLog(log, source.files).c_str());
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

GlslDialect CurrentGlslDialect() {
#if defined(WLW_GLES)
  return GlslDialect::Essl300;
#else
  return platform::CurrentContextIsES() ? GlslDialect::Essl300 : GlslDialect::Glsl330;
#endif
}

std::string DescribeFragmentPrecision() {
  std::string text = "ignored (desktop GL)";
#if defined(WLW_GLES)
  GLint range[2] = {0, 0};
  GLint precision = 0;  // bits of the significand: 10 = fp16, 23 = fp32
  glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT, range, &precision);
  text = precision >= 23 ? "32-bit: no gain here" : "16-bit";
  if (!cv_mediump.Get()) text += ", opt-ins off (r.mediump 0)";
#endif
  return text;
}

void SetShaderSourceObserver(ShaderSourceObserver observer) { g_observer = std::move(observer); }

GLuint BuildProgram(std::string_view label, const ShaderStageSource& vertex, const ShaderStageSource& fragment,
                    const std::vector<std::string>& defines) {
  WLW_PROFILE_SCOPE("Compile program");  // a hitch when it happens during play
  WLW_PROFILE_TEXT(label.data(), label.size());
  const GLuint vs = CompileStage(GL_VERTEX_SHADER, label, vertex, defines);
  const GLuint fs = vs ? CompileStage(GL_FRAGMENT_SHADER, label, fragment, defines) : 0;
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    return 0;
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);  // the linked program keeps what it needs
  glDeleteShader(fs);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(length > 1 ? static_cast<size_t>(length) : 1, '\0');
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
    WLW_LOG(LogShader, Error, "Shader %.*s (%s) failed to link:\n%s", static_cast<int>(label.size()), label.data(),
            Joined(defines).c_str(), log.c_str());
    glDeleteProgram(program);
    return 0;
  }
  // The per-frame block, wherever a program uses it, reads from the same binding point.
  const GLuint frame_block = glGetUniformBlockIndex(program, "WlwFrame");
  if (frame_block != GL_INVALID_INDEX) glUniformBlockBinding(program, frame_block, kFrameBlockBinding);
  return program;
}

GLuint BuildEngineProgram(std::string_view vertex_file, std::string_view fragment_file,
                          const std::vector<std::string>& defines) {
  const std::string* vertex = FindShaderFile(vertex_file);
  const std::string* fragment = FindShaderFile(fragment_file);
  if (!vertex || !fragment) {
    WLW_LOG(LogShader, Error, "Engine shader file missing: %.*s",
            static_cast<int>((vertex ? fragment_file : vertex_file).size()),
            (vertex ? fragment_file : vertex_file).data());
    return 0;
  }
  std::string label(vertex_file);
  label += " + ";
  label += fragment_file;
  return BuildProgram(label, {vertex_file, *vertex, {}}, {fragment_file, *fragment, {}}, defines);
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
