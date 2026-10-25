// Every engine program and a set of game shaders compile and link twice: as GLSL 3.30 (desktop) and
// as GLSL ES 3.00 in a desktop OpenGL ES 3.0 context (WindowOptions::gles), the language phones run.
// ES is where precision rules bite: a uniform or block member declared in both stages must have the
// same precision in both (rendering/glsl_header.h), and only some drivers refuse to link when it
// doesn't, so CI's Mesa catches what a phone would. Nothing is drawn.
//   wlw_gpu_tests -ts=shader_compile --dump-shaders <dir>   writes each stage's final text to
//   <dir>/glsl330 and <dir>/es300 (e.g. for the Mali Offline Compiler: malioc --core Mali-G78 x.frag)
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "golden.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl_program.h"
#include "wlw/rendering/gl_shader.h"
#include "wlw/rendering/mesh_variant.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/shader_library.h"
#include "wlw/scene/window.h"

TEST_SUITE_BEGIN("shader_compile");

using namespace wlw;
using namespace wlw::rendering;

namespace {

// Game shaders that exercise the precision rules (rendering/shader.h).
// A mesh shader with its own vertex(): a uniform and a sampler used in both stages, and lighting.
constexpr const char* kSharedUniforms = R"(shader_type mesh;
#include "wlw/lighting.glsl"
uniform float uWave;
uniform vec4 uTint;
uniform sampler2D uHeight;
void vertex() {
  vec3 p = aPos + aNormal * (textureLod(uHeight, aUV, 0.0).r + sin(aPos.x + uWave) * 0.1);
  vec4 world = WorldPosition(p);
  gl_Position = uViewProjection * world;
  vWorldPos = world.xyz;
  vNormal = WorldNormal(aNormal);
  vUV = aUV;
  vColor = aColor * uTint;
}
void fragment() {
  vec3 light = SceneLighting(vWorldPos, normalize(vNormal), ViewDirection(vWorldPos), 32.0, 1.0);
  vec4 color = vColor * texture(uHeight, vUV) * (0.5 + 0.5 * sin(uWave));
  FragColor = vec4(color.rgb * light * color.a, color.a);
}
)";

// A mediump mesh shader with vertex() that is shadowed (wlw/shadows.glsl in both stages).
constexpr const char* kShadowed = R"(shader_type mesh;
precision mediump float;
#include "wlw/lighting.glsl"
#include "wlw/shadows.glsl"
void vertex() {
  highp vec4 world = WorldPosition(aPos);
  gl_Position = uViewProjection * world;
  vWorldPos = world.xyz;
  vNormal = WorldNormal(aNormal);
  vUV = aUV;
  vColor = aColor;
}
void fragment() {
  vec3 normal = normalize(vNormal);
  float shadow = SceneShadow(vWorldPos, normal);
  FragColor = vec4(vColor.rgb * SceneLighting(vWorldPos, normal, ViewDirection(vWorldPos), 32.0, shadow), 1.0);
}
)";

// A mesh shader without vertex() (the engine's), uModel and uNormalMatrix read in the fragment stage.
constexpr const char* kDefaultVertex = R"(shader_type mesh;
uniform float uTime;
void fragment() {
  vec3 local = (inverse(uModel) * vec4(vWorldPos, 1.0)).xyz;
  vec3 up = normalize(uNormalMatrix * vec3(0.0, 1.0, 0.0));
  FragColor = vec4(vColor.rgb * fract(local + uTime) * (0.5 + 0.5 * dot(normalize(vNormal), up)), 1.0);
}
)";

// A canvas shader that opts in to mediump, keeping what needs it highp.
constexpr const char* kMediump = R"(shader_type canvas;
precision mediump float;
uniform highp float uTime;
uniform vec4 uTint;
void fragment() {
  highp vec2 pixel = vUV * uSize;
  float stripes = step(0.5, fract(pixel.x / 8.0 + uTime));
  FragColor = uTint * vColor * stripes;
}
)";

// A post shader using the sampler types GLSL ES has no default precision for.
constexpr const char* kSamplerTypes = R"(shader_type post;
uniform sampler3D uVolume;
uniform sampler2DArray uLayers;
uniform isampler2D uIds;
uniform usampler2D uMask;
uniform sampler2DShadow uDepth;
void fragment() {
  vec4 color = texture(uSource, vUV) + texture(uVolume, vec3(vUV, 0.5)) + texture(uLayers, vec3(vUV, 1.0));
  int id = texelFetch(uIds, ivec2(vUV * uOutputSize), 0).r;
  uint mask = texelFetch(uMask, ivec2(vUV * uOutputSize), 0).r;
  float lit = texture(uDepth, vec3(vUV, 0.5));
  FragColor = color * float((id & 1) + int(mask & 2u)) * lit + texture(uScene, vUV);
}
)";

std::string g_current;  // the program being built, for the dumped files' names

// Writes each stage's final text when --dump-shaders was given.
void DumpShadersTo(const std::string& subdirectory) {
  if (gpu_tests::ShaderDumpDirectory().empty()) return;
  const std::filesystem::path directory = std::filesystem::path(gpu_tests::ShaderDumpDirectory()) / subdirectory;
  std::filesystem::create_directories(directory);
  SetShaderSourceObserver([directory](std::string_view, ShaderStage stage, const std::string& text) {
    std::ofstream(directory / (g_current + (stage == ShaderStage::Vertex ? ".vert" : ".frag")), std::ios::binary)
        << text;
  });
}

std::string VariantName(const std::string& base, const std::vector<std::string>& defines) {
  std::string name = base;
  for (const std::string& define : defines) name += "_" + define;
  return name;
}

// Builds every program; each failure is a failed check (the compiler's log is in the test output).
void CompileEverything() {
  auto engine = [](const char* name, const char* vertex, const char* fragment,
                   const std::vector<std::string>& defines) {
    g_current = VariantName(name, defines);
    const GLuint program = BuildEngineProgram(vertex, fragment, defines);
    CHECK_MESSAGE(program != 0, g_current);
    if (program) glDeleteProgram(program);
  };
  for (uint32_t key = 0; key < 32; ++key) engine("mesh", "mesh.vert", "mesh.frag", MeshVariantDefines(key));
  engine("shadow", "shadow.vert", "shadow.frag", {});
  engine("shadow", "shadow.vert", "shadow.frag", {"INSTANCED"});
  engine("sprite", "sprite.vert", "sprite.frag", {});
  engine("sprite", "sprite.vert", "sprite.frag", {"TEXTURED"});
  engine("skybox", "skybox.vert", "skybox.frag", {});
  engine("ui", "ui.vert", "ui.frag", {});

  auto game = [](const std::string& name, const std::string& code) {
    g_current = name;
    const auto shader = Shader::Create(code, name);
    REQUIRE_MESSAGE(shader->IsValid(), name);
    const GLShader compiled(*shader);
    CHECK_MESSAGE(compiled.Program() != 0, name);
  };
  for (const char* post : {"post/copy.glsl", "post/blur.glsl", "post/bloom_down.glsl", "post/bloom_up.glsl",
                           "post/threshold.glsl", "post/output.glsl"}) {
    const std::string* text = FindShaderFile(post);
    REQUIRE_MESSAGE(text != nullptr, post);
    game(std::filesystem::path(post).stem().string(), *text);
  }
  game("game_shared_uniforms", kSharedUniforms);
  game("game_default_vertex", kDefaultVertex);
  game("game_shadowed", kShadowed);
  game("game_mediump", kMediump);
  game("game_sampler_types", kSamplerTypes);
}

}  // namespace

TEST_CASE("glsl 330") {
  REQUIRE_MESSAGE(gpu_tests::UseGpuContext(), "no GL 3.3 context (on Linux CI: run under xvfb-run with Mesa)");
  REQUIRE(CurrentGlslDialect() == GlslDialect::Glsl330);
  DumpShadersTo("glsl330");
  CompileEverything();
  SetShaderSourceObserver(nullptr);
}

TEST_CASE("glsl es 300") {
  // A hidden window of its own with an OpenGL ES 3.0 context (Mesa: GLX/EGL ES profiles; NVIDIA and
  // AMD on Windows: WGL_EXT_create_context_es2_profile).
  REQUIRE(platform::Init());
  auto window = scene::Window::Create({64, 64}, "wlw_gpu_tests ES", {.visible = false, .gles = true});
  REQUIRE_MESSAGE(window->Initialize(), "no OpenGL ES 3.0 context on this machine");
  window->MakeContextCurrent();
  REQUIRE(platform::LoadGLFunctions());
  MESSAGE("ES context: " << std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
  REQUIRE(CurrentGlslDialect() == GlslDialect::Essl300);
  DumpShadersTo("es300");
  CompileEverything();
  SetShaderSourceObserver(nullptr);
  window.reset();              // its context goes with it
  platform::Shutdown();        // our Init() (counted: GLFW stays up while the golden tests use it)
  gpu_tests::UseGpuContext();  // the golden tests' context back, if they run in this process too
}

TEST_SUITE_END();
