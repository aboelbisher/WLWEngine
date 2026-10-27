// Tests for custom shader text (rendering/shader.h): one text with shader_type, fragment() and an
// optional vertex(), split into the two GPU stages; instance uniforms and the node params that set them. No GPU
// needed.
#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "test_helpers.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/scene/geometry_instance.h"

TEST_SUITE_BEGIN("shader");

using wlw::rendering::InstanceParamVec4;
using wlw::rendering::InstanceUniformType;
using wlw::rendering::Shader;
using wlw::rendering::ShaderSource;
using wlw::rendering::SplitShaderSource;

static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// The line `part` is on (1 = first).
static long LineOf(const std::string& text, const std::string& part) {
  const size_t at = text.find(part);
  if (at == std::string::npos) return -1;
  return 1 + static_cast<long>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
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
  CHECK(!Has(s.fragment, "shader_type"));                               // not GLSL: removed...
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
  CHECK(!SplitShaderSource("void fragment() { FragColor = vec4(1.0); }").ok);                 // no shader_type
  CHECK(!SplitShaderSource("shader_type sprite;\nvoid fragment() {}").ok);                    // unknown type
  CHECK(!SplitShaderSource("shader_type mesh;\nvoid vertex() {}").ok);                        // no fragment()
  CHECK(!SplitShaderSource("shader_type canvas;\nvoid vertex() {}\nvoid fragment() {}").ok);  // canvas: no vertex()
  const ShaderSource open = SplitShaderSource("shader_type mesh;\nvoid fragment() { if (true) {\n}");
  CHECK((!open.ok && Has(open.error, "braces")));
}

TEST_CASE("instance uniforms") {
  const std::string code =
      "shader_type mesh;\n"
      "uniform float uGloss;\n"
      "instance uniform float uFlash;\n"
      "instance  uniform highp vec3 uTint ;\n"
      "// instance uniform vec4 uNot;\n"
      "void fragment() {\n"
      "  FragColor = vec4(uTint + vec3(uFlash + uGloss), 1.0);\n"
      "}\n";
  const ShaderSource s = SplitShaderSource(code);
  REQUIRE(s.ok);
  REQUIRE(s.instance_uniforms.size() == 2);  // not the one in a comment
  CHECK((s.instance_uniforms[0].name == "uFlash" && s.instance_uniforms[0].type == InstanceUniformType::Float));
  CHECK((s.instance_uniforms[1].name == "uTint" && s.instance_uniforms[1].type == InstanceUniformType::Vec3));
  // Plain uniforms for GLSL, on their own lines.
  CHECK((!Has(s.fragment, "instance uniform float") && Has(s.fragment, "uniform float uFlash;")));
  CHECK(Has(s.fragment, "uniform highp vec3 uTint ;"));
  CHECK(LineOf(s.fragment, "uFlash;") == LineOf(code, "uFlash;"));
  CHECK(SplitShaderSource("shader_type mesh;\nvoid fragment() {}").instance_uniforms.empty());

  auto error = [](const std::string& declaration) {
    return SplitShaderSource("shader_type mesh;\n" + declaration + "\nvoid fragment() {}").error;
  };
  CHECK(Has(error("instance uniform sampler2D uTexture;"), "isn't float"));
  CHECK(Has(error("instance uniform mat4 uMatrix;"), "isn't float"));
  CHECK(Has(error("instance uniform float uArray[2];"), "no arrays"));
  CHECK(Has(error("instance uniform float uA;\ninstance uniform vec2 uA;"), "twice"));
  std::string nine;
  for (int i = 0; i < 9; ++i) nine += "instance uniform float u" + std::to_string(i) + ";\n";
  CHECK(Has(error(nine), "more than 8"));
  nine.resize(nine.rfind("instance"));  // eight are fine
  CHECK(SplitShaderSource("shader_type mesh;\n" + nine + "void fragment() {}").instance_uniforms.size() == 8);
  CHECK(Has(SplitShaderSource("shader_type canvas;\ninstance uniform float uA;\nvoid fragment() {}").error,
            "mesh shaders"));
}

TEST_CASE("instance params on a node") {
  wlw::scene::MeshInstance3D node;
  CHECK(node.GetInstanceParams() == nullptr);  // none: nothing allocated
  CHECK(node.GetInstanceParam("uFlash") == nullptr);
  node.SetInstanceParam("uFlash", 2.0f);
  node.SetInstanceParam("uTint", glm::vec3(1.0f, 0.5f, 0.25f));
  node.SetInstanceParam("uFlash", 3.0f);  // replaces
  REQUIRE(node.GetInstanceParams() != nullptr);
  CHECK(node.GetInstanceParams()->size() == 2);
  CHECK(std::get<float>(*node.GetInstanceParam("uFlash")) == 3.0f);
  CHECK(InstanceParamVec4(*node.GetInstanceParam("uTint")) == glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));
  node.SetInstanceParam("uColor", wlw::Color{1.0f, 0.0f, 0.0f, 0.5f});  // a colour = its vec4
  CHECK(std::get<glm::vec4>(*node.GetInstanceParam("uColor")) == glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
  node.ClearInstanceParam("uFlash");
  CHECK((node.GetInstanceParam("uFlash") == nullptr && node.GetInstanceParams()->size() == 2));
  node.ClearInstanceParam("uTint");
  node.ClearInstanceParam("uColor");
  CHECK(node.GetInstanceParams() == nullptr);  // the last one gone: freed
  node.SetInstanceParam("uFlash", 1.0f);
  node.ClearInstanceParams();
  CHECK(node.GetInstanceParams() == nullptr);
}

TEST_CASE("material versions") {
  // The renderer re-uploads a material's params only when (id, version) changes.
  auto material = wlw::rendering::ShaderMaterial::Create(nullptr);
  const uint64_t start = material->Internal().Version();
  material->SetParam("uGloss", 0.5f);
  CHECK(material->Internal().Version() == start + 1);  // added
  material->SetParam("uGloss", 0.5f);
  CHECK(material->Internal().Version() == start + 1);  // the same value: no change
  material->SetParam("uGloss", 0.25f);
  CHECK(material->Internal().Version() == start + 2);
  // A copy is another material: its own id, so the two never share uploads.
  const wlw::rendering::ShaderMaterial copy = *material;
  CHECK(copy.Internal().Id() != material->Internal().Id());
  CHECK(wlw::rendering::ShaderMaterial::Create(nullptr)->Internal().Id() != material->Internal().Id());
}

TEST_CASE("array params") {
  // A uniform array's values: a new upload only when one of them changes.
  auto material = wlw::rendering::ShaderMaterial::Create(nullptr);
  const std::vector<glm::vec4> rings{{0.5f, 0.5f, 0.2f, 0.1f}, {0.0f, 0.0f, 0.0f, 0.0f}};
  material->SetParam("uRings", rings);
  const uint64_t added = material->Internal().Version();
  material->SetParam("uRings", rings);
  CHECK(material->Internal().Version() == added);
  std::vector<glm::vec4> moved = rings;
  moved[1].z = 0.3f;
  material->SetParam("uRings", moved);
  CHECK(material->Internal().Version() == added + 1);
  const auto* param = material->GetParam("uRings");
  REQUIRE(param != nullptr);
  CHECK(std::get<std::vector<glm::vec4>>(*param)[1].z == doctest::Approx(0.3f));
  material->SetParam("uWeights", std::vector<float>{0.25f, 0.5f, 0.25f});
  CHECK(std::get<std::vector<float>>(*material->GetParam("uWeights")).size() == 3);
}

TEST_CASE("variants") {
  // The defines are kept with the text; the renderer compiles them in front of it (gpu: "shader variant").
  const std::string code = "shader_type post;\nvoid fragment() {\n#ifdef GLOW\n  FragColor = vec4(1.0);\n#endif\n}\n";
  const auto plain = wlw::rendering::Shader::Create(code, "plain");
  const auto glow = wlw::rendering::Shader::Create(code, "glow", {"GLOW", "STRENGTH 2.0"});
  CHECK(plain->IsValid());
  CHECK(glow->IsValid());
  CHECK(plain->Internal().Source().defines.empty());
  CHECK(glow->Internal().Source().defines == std::vector<std::string>{"GLOW", "STRENGTH 2.0"});
}

TEST_SUITE_END();
