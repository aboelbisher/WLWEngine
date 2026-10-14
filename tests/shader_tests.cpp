// Tests for custom shader text (rendering/shader.h): one text with shader_type, fragment() and an
// optional vertex(), split into the two GPU stages. No GPU needed.
#include <algorithm>
#include <string>

#include "test_helpers.h"
#include "wlw/rendering/shader.h"

TEST_SUITE_BEGIN("shader");

using wlw::rendering::Shader;
using wlw::rendering::ShaderSource;
using wlw::rendering::SplitShaderSource;

static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// The line `part` is on (1 = first).
static long LineOf(const std::string& text, const std::string& part) {
  const size_t at = text.find(part);
  return at == std::string::npos ? -1 : 1 + std::count(text.begin(), text.begin() + static_cast<long>(at), '\n');
}

TEST_CASE("canvas") {
  const std::string code =
      "shader_type canvas;\n"
      "uniform float uTime;\n"
      "void fragment() {\n"
      "  FragColor = vec4(vUV, uTime, 1.0);\n"
      "}\n";
  const ShaderSource s = SplitShaderSource(code);
  CHECK((s.ok && s.error.empty() && s.target == Shader::Target::Canvas));
  CHECK(s.vertex.empty());  // the engine's vertex stage
  CHECK((Has(s.fragment, "uniform float uTime;") && Has(s.fragment, "FragColor = vec4(vUV, uTime, 1.0);")));
  CHECK(Has(s.fragment, "void main() { fragment(); }"));
  CHECK(!Has(s.fragment, "shader_type"));                    // not GLSL: removed...
  CHECK(LineOf(s.fragment, "FragColor") == LineOf(code, "FragColor"));  // ...without moving lines
}

TEST_CASE("mesh with vertex") {
  const std::string code =
      "// a wavy flag\n"
      "shader_type mesh;\n"
      "uniform float uTime;\n"
      "float Wave(float x) { return sin(x + uTime); }\n"
      "void vertex() {\n"
      "  vec3 p = aPos + vec3(0.0, Wave(aPos.x), 0.0);\n"
      "  if (uInstanced) { p.y += 0.0; }\n"
      "  gl_Position = uProjection * uView * uModel * vec4(p, 1.0);\n"
      "}\n"
      "void fragment() {\n"
      "  FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
  const ShaderSource s = SplitShaderSource(code);
  CHECK((s.ok && s.target == Shader::Target::Mesh));
  // Each stage: the shared parts, its own function, not the other's.
  CHECK((Has(s.vertex, "float Wave") && Has(s.vertex, "gl_Position =") && !Has(s.vertex, "FragColor")));
  CHECK(Has(s.vertex, "void main() { vertex(); }"));
  CHECK((Has(s.fragment, "float Wave") && Has(s.fragment, "FragColor =") && !Has(s.fragment, "gl_Position")));
  CHECK(!Has(s.fragment, "if (uInstanced)"));  // the whole vertex() went, nested braces too
  CHECK(LineOf(s.fragment, "FragColor") == LineOf(code, "FragColor"));
  CHECK(LineOf(s.vertex, "gl_Position") == LineOf(code, "gl_Position"));
}

TEST_CASE("comments") {
  // A vertex() in a comment isn't one; braces in comments don't count.
  const std::string code =
      "shader_type canvas;\n"
      "// void vertex() { }\n"
      "/* shader_type mesh; */\n"
      "void fragment() {\n"
      "  // } a brace in a comment\n"
      "  FragColor = vColor;\n"
      "}\n";
  const ShaderSource s = SplitShaderSource(code);
  CHECK((s.ok && s.target == Shader::Target::Canvas && s.vertex.empty()));
  CHECK(Has(s.fragment, "FragColor = vColor;"));
}

TEST_CASE("errors") {
  CHECK(!SplitShaderSource("void fragment() { FragColor = vec4(1.0); }").ok);  // no shader_type
  CHECK(!SplitShaderSource("shader_type sprite;\nvoid fragment() {}").ok);       // unknown type
  CHECK(!SplitShaderSource("shader_type mesh;\nvoid vertex() {}").ok);           // no fragment()
  CHECK(!SplitShaderSource("shader_type canvas;\nvoid vertex() {}\nvoid fragment() {}").ok);  // canvas: no vertex()
  const ShaderSource open = SplitShaderSource("shader_type mesh;\nvoid fragment() { if (true) {\n}");
  CHECK((!open.ok && Has(open.error, "braces")));
}

TEST_SUITE_END();
