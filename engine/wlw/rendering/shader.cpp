#include "wlw/rendering/shader.h"

#include <regex>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace wlw::rendering {

namespace {

constexpr size_t kNone = std::string::npos;

// The text with its comments turned into spaces (newlines kept): same length and line numbers,
// so positions found in it are positions in the text, and nothing in a comment is matched.
std::string WithoutComments(const std::string& code) {
  std::string out = code;
  for (size_t i = 0; i + 1 < out.size(); ++i) {
    if (out[i] == '/' && out[i + 1] == '/') {
      while (i < out.size() && out[i] != '\n') out[i++] = ' ';
    } else if (out[i] == '/' && out[i + 1] == '*') {
      out[i] = out[i + 1] = ' ';
      i += 2;
      while (i < out.size() && !(out[i] == '*' && i + 1 < out.size() && out[i + 1] == '/')) {
        if (out[i] != '\n') out[i] = ' ';
        ++i;
      }
      if (i < out.size()) out[i] = ' ';
      if (i + 1 < out.size()) out[i + 1] = ' ';
      ++i;
    }
  }
  return out;
}

// Turns [begin, end) into spaces, keeping the newlines.
void Blank(std::string& text, size_t begin, size_t end) {
  for (size_t i = begin; i < end && i < text.size(); ++i) {
    if (text[i] != '\n') text[i] = ' ';
  }
}

// Where `void name()` is, from `void` to its closing brace (end is one past it). begin = kNone if
// it isn't there; end = kNone if its braces don't close.
struct Span {
  size_t begin = kNone;
  size_t end = kNone;
};
Span FindFunction(const std::string& masked, const std::string& name) {
  Span span;
  std::smatch match;
  const std::regex signature("\\bvoid\\s+" + name + "\\s*\\(\\s*(void\\s*)?\\)");
  if (!std::regex_search(masked, match, signature)) return span;
  span.begin = static_cast<size_t>(match.position(0));
  const size_t open = masked.find('{', span.begin + match.length(0));
  if (open == kNone) return span;
  int depth = 0;
  for (size_t i = open; i < masked.size(); ++i) {
    if (masked[i] == '{') ++depth;
    if (masked[i] == '}' && --depth == 0) {
      span.end = i + 1;
      break;
    }
  }
  return span;
}

}  // namespace

ShaderSource SplitShaderSource(const std::string& code) {
  ShaderSource result;
  const std::string masked = WithoutComments(code);

  std::smatch match;
  if (!std::regex_search(masked, match, std::regex("\\bshader_type\\s+(\\w+)\\s*;"))) {
    result.error = "missing 'shader_type canvas;', 'shader_type mesh;' or 'shader_type post;'";
    return result;
  }
  const std::string type = match[1].str();
  if (type == "canvas") {
    result.target = Shader::Target::Canvas;
  } else if (type == "mesh") {
    result.target = Shader::Target::Mesh;
  } else if (type == "post") {
    result.target = Shader::Target::Post;
  } else {
    result.error = "unknown shader_type '" + type + "' (canvas, mesh or post)";
    return result;
  }
  std::string text = code;
  Blank(text, static_cast<size_t>(match.position(0)), static_cast<size_t>(match.position(0) + match.length(0)));

  const Span fragment = FindFunction(masked, "fragment");
  const Span vertex = FindFunction(masked, "vertex");
  if (fragment.begin == kNone) {
    result.error = "missing 'void fragment()'";
    return result;
  }
  if (fragment.end == kNone || (vertex.begin != kNone && vertex.end == kNone)) {
    result.error = "unbalanced braces";
    return result;
  }
  if (vertex.begin != kNone && result.target == Shader::Target::Canvas) {
    result.error = "canvas shaders have no vertex() (the engine's draws the rectangle)";
    return result;
  }
  if (vertex.begin != kNone && result.target == Shader::Target::Post) {
    result.error = "post shaders have no vertex() (the engine's covers the picture)";
    return result;
  }

  result.fragment = text;
  if (vertex.begin != kNone) {
    Blank(result.fragment, vertex.begin, vertex.end);
    result.vertex = text;
    Blank(result.vertex, fragment.begin, fragment.end);
    result.vertex += "\nvoid main() { vertex(); }\n";
  }
  result.fragment += "\nvoid main() { fragment(); }\n";
  result.ok = true;
  return result;
}

Shader::Shader(ShaderSource source, std::string name) : source_(std::move(source)), name_(std::move(name)) {}

std::unique_ptr<Shader> Shader::Create(const std::string& code, const std::string& name) {
  ShaderSource source = SplitShaderSource(code);
  if (!source.ok) WLW_LOG(LogShader, Error, "%s: %s", name.c_str(), source.error.c_str());
  return std::unique_ptr<Shader>(new Shader(std::move(source), name));
}

std::unique_ptr<Shader> Shader::FromFile(const std::string& path) {
  const auto bytes = platform::ReadFile(path);
  if (!bytes) {
    WLW_LOG(LogShader, Error, "Shader file not found: %s", path.c_str());
    ShaderSource missing;
    missing.error = "file not found";
    return std::unique_ptr<Shader>(new Shader(std::move(missing), path));
  }
  return Create(std::string(bytes->begin(), bytes->end()), path);
}

}  // namespace wlw::rendering
