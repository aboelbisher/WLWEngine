// Tests for the shader system's CPU side: the preprocessor (#include, #define, #line) and log
// mapping, the embedded engine shader files (every stage preprocesses in every variant), the
// per-frame block's std140 layout and light packing, mesh variant keys, the UI quad kinds agreeing
// with ui.frag, the GLSL headers per stage and dialect, and the precision lint (every uniform, block
// member and varying of the engine's files names its precision). Compiling the GLSL needs a GPU:
// tests/gpu/shader_compile_tests.cpp, as GLSL 3.30 and as GLSL ES 3.00.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <regex>
#include <set>
#include <sstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/rendering/frame_uniforms.h"
#include "wlw/rendering/glsl_header.h"
#include "wlw/rendering/mesh_variant.h"
#include "wlw/rendering/shader_library.h"
#include "wlw/rendering/shader_preprocessor.h"
#include "wlw/scene/light.h"
#include "wlw/ui/canvas.h"

TEST_SUITE_BEGIN("shader_system");

using namespace wlw::rendering;

namespace {

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// A lookup over a fixed set of files.
ShaderFileLookup Files(const std::map<std::string, std::string, std::less<>>& files) {
  return [&files](std::string_view name) -> const std::string* {
    const auto it = files.find(name);
    return it == files.end() ? nullptr : &it->second;
  };
}

}  // namespace

TEST_CASE("includes") {
  const std::map<std::string, std::string, std::less<>> files = {
      {"a.glsl", "float A() { return 1.0; }\n"},
      {"b.glsl", "#include \"a.glsl\"\nfloat B() { return A(); }\n"},
  };
  const PreprocessedShader out =
      PreprocessShader("main", "#include \"b.glsl\"\n#include \"a.glsl\"  // already in\nvoid main() {}\n",
                       {"LIT", "N 4"}, {}, Files(files));
  CHECK(out.ok);
  // Defines first, in order; each file once; the main text keeps its line numbers.
  CHECK(out.code.rfind("#define LIT\n#define N 4\n#line 1 0\n", 0) == 0);
  CHECK((out.code.find("float A()") != std::string::npos && out.code.find("float A()") == out.code.rfind("float A()")));
  CHECK(out.code.find("float A()") < out.code.find("float B()"));
  CHECK((out.files == std::vector<std::string>{"main", "b.glsl", "a.glsl"}));
  CHECK((Contains(out.code, "#line 1 1\n") && Contains(out.code, "#line 1 2\n")));
  CHECK(Contains(out.code, "#line 2 1\n"));  // back in b.glsl after its include
  CHECK(Contains(out.code, "#line 2 0\n"));  // back in main after its first include
  // The second include of a.glsl is an empty line, so "void main" is still main's line 3.
  const size_t main_line = out.code.find("void main");
  CHECK(out.code.substr(out.code.rfind("#line 2 0\n", main_line)).rfind("\n\nvoid main", 20) != std::string::npos);
  CHECK(!Contains(out.code, "#include"));
}

TEST_CASE("prelude") {
  const std::map<std::string, std::string, std::less<>> files = {{"inputs.glsl", "in vec2 vUV;\n"}};
  const PreprocessedShader out = PreprocessShader("shader", "void fragment() {}\n", {}, {"inputs.glsl"}, Files(files));
  CHECK(out.ok);
  CHECK(out.code.find("in vec2 vUV;") <
        out.code.find("#line 1 0\nvoid fragment"));  // inputs first, then the text at its line 1
}

TEST_CASE("include errors") {
  const std::map<std::string, std::string, std::less<>> files = {
      {"loop_a.glsl", "#include \"loop_b.glsl\"\n"},
      {"loop_b.glsl", "#include \"loop_a.glsl\"\n"},
      {"self.glsl", "#include \"self.glsl\"\n"},
  };
  const PreprocessedShader missing = PreprocessShader("main", "\n#include \"nope.glsl\"\n", {}, {}, Files(files));
  CHECK((!missing.ok && Contains(missing.error, "main:2") && Contains(missing.error, "nope.glsl")));
  const PreprocessedShader cycle = PreprocessShader("main", "#include \"loop_a.glsl\"\n", {}, {}, Files(files));
  CHECK((!cycle.ok && Contains(cycle.error, "cycle") &&
         Contains(cycle.error, "loop_a.glsl -> loop_b.glsl -> loop_a.glsl")));
  const PreprocessedShader self = PreprocessShader("main", "#include \"self.glsl\"\n", {}, {}, Files(files));
  CHECK((!self.ok && Contains(self.error, "cycle")));
  const PreprocessedShader malformed = PreprocessShader("main", "#include <a.glsl>\n", {}, {}, Files(files));
  CHECK((!malformed.ok && Contains(malformed.error, "malformed")));
  // Other directives pass through untouched.
  const PreprocessedShader plain = PreprocessShader("main", "#ifdef X\n#endif\n", {}, {}, Files(files));
  CHECK((plain.ok && Contains(plain.code, "#ifdef X\n#endif\n")));
}

TEST_CASE("map shader log") {
  const std::vector<std::string> files = {"mesh.frag", "wlw/frame.glsl", "wlw/lighting.glsl"};
  CHECK(MapShaderLog("ERROR: 2:14: 'x' : undeclared", files) == "ERROR: wlw/lighting.glsl:14: 'x' : undeclared");
  CHECK(MapShaderLog("0(7) : error C1008: undefined variable", files) ==
        "mesh.frag:7 : error C1008: undefined variable");
  CHECK(MapShaderLog("ERROR: 9:3: unknown file", files) == "ERROR: 9:3: unknown file");  // not ours: left alone
  CHECK(MapShaderLog("no positions here", files) == "no positions here");
}

TEST_CASE("engine shader files") {
  for (const char* name : {"mesh.vert",
                           "mesh.frag",
                           "shadow.vert",
                           "shadow.frag",
                           "skybox.vert",
                           "skybox.frag",
                           "ui.vert",
                           "ui.frag",
                           "wlw/frame.glsl",
                           "wlw/lighting.glsl",
                           "wlw/shadows.glsl",
                           "wlw/common.glsl",
                           "wlw/color.glsl",
                           "custom/canvas.vert",
                           "custom/canvas_fragment.glsl",
                           "custom/mesh_vertex.glsl",
                           "custom/mesh_default.vert",
                           "custom/mesh_fragment.glsl",
                           "sprite.vert",
                           "sprite.frag"}) {
    if (!FindShaderFile(name)) std::printf("  missing engine shader file %s\n", name);
    CHECK(FindShaderFile(name) != nullptr);
  }
  CHECK(FindShaderFile("nope.glsl") == nullptr);
  CHECK(!Contains(*FindShaderFile("mesh.frag"), "\r"));  // line endings normalized

  // Every stage preprocesses (all includes found, no cycles) in every variant, as each stage.
  auto check_stage = [](const char* file, const std::vector<std::string>& defines) {
    const PreprocessedShader out = PreprocessShader(file, *FindShaderFile(file), defines);
    if (!out.ok) std::printf("  %s: %s\n", file, out.error.c_str());
    CHECK(out.ok);
  };
  for (uint32_t key = 0; key < 32; ++key) {
    std::vector<std::string> defines = MeshVariantDefines(key);
    defines.emplace_back("WLW_VERTEX");
    check_stage("mesh.vert", defines);
    defines.back() = "WLW_FRAGMENT";
    check_stage("mesh.frag", defines);
  }
  for (const std::vector<std::string>& variant : {std::vector<std::string>{}, std::vector<std::string>{"TEXTURED"}}) {
    std::vector<std::string> defines = variant;  // the sprite batches' program, plain and textured
    defines.emplace_back("WLW_VERTEX");
    check_stage("sprite.vert", defines);
    defines.back() = "WLW_FRAGMENT";
    check_stage("sprite.frag", defines);
  }
  for (const char* file : {"shadow.vert", "skybox.vert", "ui.vert", "custom/canvas.vert"})
    check_stage(file, {"WLW_VERTEX"});
  for (const char* file : {"shadow.frag", "skybox.frag", "ui.frag"}) check_stage(file, {"WLW_FRAGMENT"});
  // A game's own include file is found, and replaces nothing of the engine's.
  AddShaderFile("game/waves.glsl", "float Wave(float x) { return sin(x); }\n");
  const PreprocessedShader game = PreprocessShader(
      "shader", "#include \"game/waves.glsl\"\n#include \"wlw/lighting.glsl\"\n", {}, {"custom/mesh_fragment.glsl"});
  CHECK((game.ok && Contains(game.code, "float Wave") && Contains(game.code, "SceneLighting")));
}

TEST_CASE("frame block layout") {
  // std140: every member here is a whole number of 16-byte slots, so these are WlwFrame's offsets.
  CHECK(offsetof(FrameUniforms, view) == 0);
  CHECK(offsetof(FrameUniforms, projection) == 64);
  CHECK(offsetof(FrameUniforms, view_projection) == 128);
  CHECK(offsetof(FrameUniforms, light_space) == 192);
  CHECK(offsetof(FrameUniforms, camera_position) == 256);
  CHECK(offsetof(FrameUniforms, view_forward) == 272);
  CHECK(offsetof(FrameUniforms, shadow_params) == 288);
  CHECK(offsetof(FrameUniforms, light_counts) == 304);
  CHECK(offsetof(FrameUniforms, directional) == 320);
  CHECK(offsetof(FrameUniforms, point) == 320 + 4 * 32);
  CHECK(offsetof(FrameUniforms, spot) == 448 + 4 * 48);
  CHECK((sizeof(FrameUniforms::Directional) == 32 && sizeof(FrameUniforms::Point) == 48 &&
         sizeof(FrameUniforms::Spot) == 80 && sizeof(FrameUniforms) == 960));

  // ...and frame.glsl declares the same members in the same order.
  const std::string& glsl = *FindShaderFile("wlw/frame.glsl");
  size_t last = 0;
  for (const char* member :
       {"mat4 uView;", "mat4 uProjection;", "mat4 uViewProjection;", "mat4 uLightSpace;", "vec3 uCameraPos;",
        "float uOrthographic;", "vec3 uViewForward;", "float uShadowStrength;", "vec4 uShadowParams;",
        "ivec4 uLightCounts;", "uDirectionalLights[WLW_MAX_LIGHTS_PER_TYPE];", "uPointLights[WLW_MAX_LIGHTS_PER_TYPE];",
        "uSpotLights[WLW_MAX_LIGHTS_PER_TYPE];"}) {
    const size_t at = glsl.find(member);
    if (at == std::string::npos || at < last) std::printf("  frame.glsl: %s missing or out of order\n", member);
    CHECK((at != std::string::npos && at >= last));
    last = at;
  }
  CHECK(Contains(glsl, "#define WLW_MAX_LIGHTS_PER_TYPE " + std::to_string(kMaxLightsPerType)));
}

TEST_CASE("frame lights") {
  auto sun = std::make_shared<wlw::scene::DirectionalLight3D>();
  sun->SetDirection({0.0f, -1.0f, 0.0f});
  sun->SetColor({1.0f, 0.5f, 0.25f});
  sun->SetIntensity(2.0f);
  sun->SetAmbient(0.3f);
  auto fill = std::make_shared<wlw::scene::DirectionalLight3D>();
  auto bulb = std::make_shared<wlw::scene::PointLight3D>();
  bulb->SetPosition({1.0f, 2.0f, 3.0f});
  bulb->SetRange(10.0f);
  auto spot = std::make_shared<wlw::scene::SpotLight3D>();
  spot->SetInnerAngle(glm::radians(10.0f));
  spot->SetOuterAngle(glm::radians(20.0f));

  FrameUniforms frame;
  SetFrameLights(frame, {fill.get(), bulb.get(), sun.get(), spot.get()}, sun.get());
  CHECK(frame.light_counts == glm::ivec4(2, 1, 1, 1));  // the sun is the second directional light
  CHECK((Near(frame.directional[1].color.r, 2.0f) && Near(frame.directional[1].color.g, 1.0f)));  // colour x intensity
  CHECK(
      (Near(frame.directional[1].direction_ambient.y, -1.0f) && Near(frame.directional[1].direction_ambient.w, 0.3f)));
  CHECK((Near(frame.point[0].position_ambient.x, 1.0f) && Near(frame.point[0].position_ambient.z, 3.0f)));
  CHECK((Near(frame.point[0].attenuation.y, 0.45f) && Near(frame.point[0].attenuation.z, 0.75f)));  // ~1% at the range
  CHECK((Near(frame.spot[0].cone.x, std::cos(glm::radians(10.0f))) &&
         Near(frame.spot[0].cone.y, std::cos(glm::radians(20.0f)))));

  // No shadow light: w = -1. More than the block holds: the extra ones are dropped.
  std::vector<std::shared_ptr<wlw::scene::DirectionalLight3D>> many(6);
  std::vector<const wlw::scene::Light3D*> lights;
  for (auto& light : many) {
    light = std::make_shared<wlw::scene::DirectionalLight3D>();
    lights.push_back(light.get());
  }
  SetFrameLights(frame, lights, many[5].get());
  CHECK(frame.light_counts == glm::ivec4(kMaxLightsPerType, 0, 0, -1));  // the 6th (shadow) light didn't fit
  CHECK((Near(AttenuationFor(0.0f).y, 0.0f) && Near(AttenuationFor(0.0f).x, 1.0f)));  // no range: no falloff
}

TEST_CASE("mesh variants") {
  MeshDraw draw;
  CHECK((MeshVariantKey(draw) == 0 && MeshVariantDefines(0).empty()));
  draw.instanced = draw.textured = draw.blends = true;
  CHECK(MeshVariantKey(draw) == (kMeshInstanced | kMeshTextured | kMeshBlend));
  // Shadows need lights, a shadow map this frame, and a node that receives them.
  draw.receives_shadow = true;
  draw.shadow_active = true;
  CHECK((MeshVariantKey(draw) & kMeshShadows) == 0);  // unlit
  draw.scene_lit = true;
  CHECK((MeshVariantKey(draw) & kMeshShadows) != 0);
  draw.shadow_active = false;
  CHECK((MeshVariantKey(draw) & kMeshShadows) == 0);
  CHECK((MeshVariantDefines(kMeshInstanced | kMeshTextured | kMeshLit | kMeshShadows | kMeshBlend) ==
         std::vector<std::string>{"INSTANCED", "TEXTURED", "LIT", "SHADOWS", "BLEND"}));
}

TEST_CASE("ui kinds match the shader") {
  const std::string& ui = *FindShaderFile("ui.frag");
  CHECK(Contains(ui, "#define KIND_TEXT " + std::to_string(static_cast<int>(wlw::ui::QuadKind::Text)) + "\n"));
  CHECK(Contains(ui, "#define KIND_RECT " + std::to_string(static_cast<int>(wlw::ui::QuadKind::Rect)) + "\n"));
  CHECK(Contains(ui, "#define KIND_IMAGE " + std::to_string(static_cast<int>(wlw::ui::QuadKind::Image)) + "\n"));
  CHECK(Contains(ui, "#define KIND_TRIANGLE " + std::to_string(static_cast<int>(wlw::ui::QuadKind::Triangle)) + "\n"));
  CHECK(Contains(ui, "#define KIND_ARC " + std::to_string(static_cast<int>(wlw::ui::QuadKind::Arc)) + "\n"));
}

TEST_CASE("glsl headers") {
  for (const ShaderStage stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
    CHECK(GlslHeader(GlslDialect::Glsl330, stage) == "#version 330 core\n");  // desktop ignores precision
  }
  // Both stages highp (a mediump fragment default made Bluzzle's lit 3D 8% slower on Adreno 640):
  // shaders opt in to mediump themselves.
  const std::string vertex = GlslHeader(GlslDialect::Essl300, ShaderStage::Vertex);
  const std::string fragment = GlslHeader(GlslDialect::Essl300, ShaderStage::Fragment);
  CHECK(vertex.rfind("#version 300 es\nprecision highp float;\nprecision highp int;\n", 0) == 0);
  CHECK(fragment == vertex);
  // Every sampler type has a default (ES has none for 3D, array, shadow and integer samplers), the
  // same in both stages: a sampler declared in both must match.
  for (const char* line :
       {"precision mediump sampler2D;", "precision mediump sampler3D;", "precision mediump samplerCube;",
        "precision mediump sampler2DArray;", "precision highp sampler2DShadow;", "precision highp samplerCubeShadow;",
        "precision highp sampler2DArrayShadow;", "precision highp isampler2D;", "precision highp isampler3D;",
        "precision highp isamplerCube;", "precision highp isampler2DArray;", "precision highp usampler2D;",
        "precision highp usampler3D;", "precision highp usamplerCube;", "precision highp usampler2DArray;"}) {
    CHECK(Contains(vertex, std::string(line) + "\n"));
  }
}

TEST_CASE("mediump opt-ins") {
  // r.mediump 0 turns a shader's own `precision mediump float;` into highp; single declarations keep theirs.
  const std::string code =
      "precision mediump float;\nin mediump vec4 vColor;\nvoid f() {}\nprecision  mediump   float ;\n// mediump\n";
  CHECK(ApplyMediumpSetting(code, true) == code);
  const std::string off = ApplyMediumpSetting(code, false);
  CHECK(!Contains(off, "precision mediump"));
  CHECK(!Contains(off, "mediump   float"));
  CHECK(Contains(off, "precision highp float;\nin mediump vec4 vColor;"));
  CHECK(std::count(off.begin(), off.end(), '\n') == std::count(code.begin(), code.end(), '\n'));  // lines kept
  CHECK(ApplyMediumpSetting("precision mediump int;\n", false) == "precision mediump int;\n");    // floats only
}

namespace {

// The text without comments and preprocessor lines (every #ifdef branch is checked at once).
std::string CodeOnly(const std::string& text) {
  std::string out;
  bool in_comment = false;
  std::istringstream lines(text);
  for (std::string line; std::getline(lines, line);) {
    std::string kept;
    for (size_t i = 0; i < line.size(); ++i) {
      if (in_comment) {
        if (line.compare(i, 2, "*/") == 0) {
          in_comment = false;
          ++i;
        }
      } else if (line.compare(i, 2, "/*") == 0) {
        in_comment = true;
        ++i;
      } else if (line.compare(i, 2, "//") == 0) {
        break;
      } else {
        kept += line[i];
      }
    }
    const size_t first = kept.find_first_not_of(" \t");
    if (first != std::string::npos && kept[first] == '#') continue;
    out += kept + "\n";
  }
  return out;
}

// A statement's words, without its layout(...).
std::vector<std::string> Words(const std::string& statement) {
  std::vector<std::string> words;
  std::istringstream in(std::regex_replace(statement, std::regex(R"(layout\s*\([^)]*\))"), " "));
  for (std::string word; in >> word;) words.push_back(word);
  return words;
}

bool Has(const std::vector<std::string>& words, const char* word) {
  return std::find(words.begin(), words.end(), word) != words.end();
}

// The declarations in a shader file that don't name their precision: uniforms, varyings, and the
// members of structs and uniform blocks. `vertex` / `fragment` = which stage the file is: a vertex
// stage's inputs are attributes and a fragment stage's output is the colour (no precision to
// match). `declarations` counts what was checked.
std::vector<std::string> UnqualifiedDeclarations(const std::string& text, bool vertex, bool fragment,
                                                 int& declarations) {
  const std::string code = CodeOnly(text);
  std::set<std::string> structs;  // struct-typed members: their own members carry the precision
  const std::regex struct_name(R"(\bstruct\s+(\w+))");
  for (auto it = std::sregex_iterator(code.begin(), code.end(), struct_name); it != std::sregex_iterator(); ++it) {
    structs.insert((*it)[1].str());
  }
  enum class Scope { Members, Code };
  std::vector<Scope> scopes;
  std::vector<std::string> missing;
  std::string statement;
  auto check = [&](bool member) {
    const std::vector<std::string> words = Words(statement);
    if (words.empty()) return;
    if (!member) {
      const bool in = Has(words, "in"), out = Has(words, "out");
      if (!Has(words, "uniform") && !in && !out) return;  // functions, constants, precision statements
      if ((in && vertex) || (out && fragment)) return;
    }
    ++declarations;
    if (Has(words, "bool")) return;  // no precision
    for (const std::string& word : words) {
      if (structs.count(word) != 0) return;
    }
    if (!Has(words, "highp") && !Has(words, "mediump")) {
      missing.push_back(std::regex_replace(statement, std::regex(R"(\s+)"), " "));
    }
  };
  for (const char c : code) {
    if (c == '{') {
      const std::vector<std::string> words = Words(statement);
      const bool members = !words.empty() && (words[0] == "struct" || words[0] == "uniform");
      scopes.push_back(scopes.empty() && members ? Scope::Members : Scope::Code);
      statement.clear();
    } else if (c == '}') {
      if (!scopes.empty()) scopes.pop_back();
      statement.clear();
    } else if (c == ';') {
      if (scopes.empty()) {
        check(false);
      } else if (scopes.back() == Scope::Members) {
        check(true);
      }
      statement.clear();
    } else {
      statement += c;
    }
  }
  return missing;
}

bool EndsWith(const std::string& text, const std::string& end) {
  return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

}  // namespace

TEST_CASE("precision lint") {
  // GLSL ES: a uniform or block member declared in both stages needs the same precision in both, and
  // the stages' defaults differ (glsl_header.h), so every engine declaration names its own.
  int files = 0, declarations = 0;
  for (const EmbeddedShaderFile& file : EmbeddedShaderFiles()) {
    const std::string name = file.name;
    const std::string text = file.source;
    if (Contains(text, "shader_type")) continue;  // game-format shaders (post/*): their preludes name theirs
    const bool vertex = EndsWith(name, ".vert") || EndsWith(name, "_vertex.glsl");
    const bool fragment = EndsWith(name, ".frag") || EndsWith(name, "_fragment.glsl");
    ++files;
    for (const std::string& declaration : UnqualifiedDeclarations(text, vertex, fragment, declarations)) {
      FAIL_CHECK(name << ": no highp / mediump on '" << declaration << "'");
    }
  }
  CHECK(files >= 20);
  CHECK(declarations >= 60);  // it saw them: the frame block, the varyings, the uniforms

  // The lint itself.
  int count = 0;
  CHECK(UnqualifiedDeclarations("uniform float a;\nuniform highp float b;\n", false, false, count).size() == 1);
  CHECK(UnqualifiedDeclarations("in vec3 aPos;\nout vec2 vUV;\n", true, false, count).size() == 1);
  CHECK(UnqualifiedDeclarations("in vec2 vUV;\nout vec4 FragColor;\n", false, true, count).size() == 1);
  CHECK(UnqualifiedDeclarations("struct S {\n  vec4 a;\n};\nlayout(std140) uniform B {\n  S s[4];\n  mediump vec3 "
                                "c;\n  mat4 m;\n};\nuniform bool uFlag;\n",
                                false, false, count)
            .size() == 2);  // S::a and B::m
  CHECK(UnqualifiedDeclarations("void f() {\n  vec3 local = vec3(0.0);\n}\n// uniform float c;\n#define X 1\n", false,
                                false, count)
            .empty());

  // A game's text after the preludes is highp unless it opts in (rendering/shader.h).
  for (const char* prelude :
       {"custom/canvas_fragment.glsl", "custom/mesh_fragment.glsl", "custom/post_fragment.glsl"}) {
    std::string code = CodeOnly(*FindShaderFile(prelude));
    code.erase(code.find_last_not_of("\n \t") + 1);
    CHECK_MESSAGE(EndsWith(code, "precision highp float;"), prelude);
  }
}

TEST_SUITE_END();
