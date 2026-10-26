// Golden-image scenes: one per renderer feature (golden.h). Everything is fixed: no time, no random
// numbers, fixed cameras, so the same GPU draws the same pixels every run. Z up, +Y forward (core/math.h):
// 2.4 turned every scene from Y up by the same quarter turn about X, and the pictures stayed the same.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "golden.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/light.h"
#include "wlw/scene/node.h"
#include "wlw/scene/post_effects.h"
#include "wlw/scene/sprite_batch.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"
#include "wlw/utils/primitives.h"
#include "wlw/utils/textures.h"

TEST_SUITE_BEGIN("golden");

using namespace wlw;
using gpu_tests::CheckGolden;
using gpu_tests::MakeViewport;

namespace {

// A perspective camera (45 degrees, looking from `eye` at `target`).
std::shared_ptr<scene::Camera3D> FixedCamera(const Vec3& eye, const Vec3& target) {
  auto camera = scene::Camera3D::Create();
  camera->SetPerspective(glm::radians(45.0f), 0.1f, 100.0f);
  camera->SetPosition(eye);
  camera->LookAt(target);
  return camera;
}

// An orthographic camera from `eye` looking at `target`, `size` x `size` world units in view.
std::shared_ptr<scene::Camera3D> OrthoCamera(const Vec3& eye, const Vec3& target, float size) {
  auto camera = scene::Camera3D::Create();
  camera->SetOrthographic(size, 0.1f, 50.0f);
  camera->SetFitMode(scene::Camera3D::FitMode::Contain);
  camera->SetPosition(eye);
  camera->LookAt(target);
  return camera;
}

using Model = core::Model<core::Vertex3D>;

std::shared_ptr<Model> MakeModel(std::shared_ptr<core::Mesh<core::Vertex3D>> mesh) {
  auto model = std::make_shared<Model>();
  model->meshes.push_back(std::move(mesh));
  return model;
}

template <typename T = scene::MeshInstance3D>
std::shared_ptr<T> AddMesh(scene::Viewport& viewport, std::shared_ptr<core::Mesh<core::Vertex3D>> mesh,
                           std::shared_ptr<rendering::Material> material, const Vec3& position) {
  auto node = std::make_shared<T>();
  node->SetModel(MakeModel(std::move(mesh)));
  node->SetMaterial(std::move(material));
  node->SetPosition(position);
  viewport.GetScene()->Root().AddChild(node);
  return node;
}

std::shared_ptr<scene::DirectionalLight3D> AddSun(scene::Viewport& viewport, float ambient = 0.25f) {
  auto sun = std::make_shared<scene::DirectionalLight3D>();
  sun->SetDirection({-0.4f, 0.6f, -1.0f});
  sun->SetAmbient(ambient);
  viewport.GetScene()->Root().AddChild(sun);
  return sun;
}

std::shared_ptr<rendering::Material> Opaque() { return rendering::Material::Create(); }

// A cube, a bevelled cube and a sphere under a sun and an orange point light.
void AddLitCubes(scene::Viewport& viewport) {
  viewport.SetCamera(FixedCamera(Vec3{0, -7, 3}, Vec3{0, 0, 0}));
  AddSun(viewport);
  auto point = std::make_shared<scene::PointLight3D>();
  point->SetPosition({2.0f, -2.0f, 1.5f});
  point->SetColor({1.0f, 0.6f, 0.2f});
  point->SetRange(6.0f);
  viewport.GetScene()->Root().AddChild(point);
  auto material = Opaque();
  material->shininess = 32.0f;
  AddMesh(viewport, utils::CubeMesh(1.2f, Color{0.9f, 0.3f, 0.3f, 1.0f}), material, {-1.8f, 0, 0})
      ->SetEulerAngles(glm::radians(wlw::Vec3(0, 0, 30)), wlw::EulerOrder::XZY);
  AddMesh(viewport, utils::BeveledCubeMesh(), material, {0, 0, 0})
      ->SetEulerAngles(glm::radians(wlw::Vec3(20, 0, 45)), wlw::EulerOrder::XZY);
  AddMesh(viewport, utils::SphereMesh(0.7f, 16, 32), material, {1.8f, 0, 0});
}

// White text and a thin line along the bottom: what stays sharp at any render scale (Overlay).
// The canvas keeps a pointer to `font` until it's drawn: the caller keeps it alive.
void AddSharpLabel(ui::Canvas& canvas, const ui::Font& font, const char* text) {
  canvas.Line({16, 236}, {240, 236}, 1.5f, Color{1.0f, 1.0f, 1.0f, 1.0f});
  canvas.Text(font, text, {128, 222}, 26, ui::Align::Center, {.color = {1, 1, 1, 1}});
}

}  // namespace

TEST_CASE("lit cubes") {
  auto viewport = MakeViewport();
  AddLitCubes(*viewport);
  CheckGolden("lit_cubes", viewport);
}

TEST_CASE("render scale") {
  // The lit cubes drawn at half size (128 x 128) and stretched back with a bilinear copy: softer
  // edges. The Overlay canvas is drawn after the stretch, at full size: sharp.
  auto viewport = MakeViewport();
  AddLitCubes(*viewport);
  viewport->SetRenderScale(0.5f);
  const float size = gpu_tests::kSize;
  viewport->Internal().BeginCanvases({size, size}, {size, size});
  const auto font = ui::Font::Load(WLW_TEST_FONT);
  REQUIRE(font != nullptr);
  AddSharpLabel(viewport->GetCanvas(), *font, "x0.5");
  CheckGolden("render_scale", viewport);
  CHECK((viewport->GetRenderStats().render_width == 128 && viewport->GetRenderStats().post_passes == 1));
}

TEST_CASE("render scale with post") {
  // Bloom on a picture drawn at 0.5: its levels keep their sizes (from half the viewport's: 128, 64,
  // 32, 16) and the output pass writes the full-size picture (no extra copy). A Picture layer is drawn into the
  // small picture (bloomed, soft); the Overlay layer stays sharp.
  auto viewport = MakeViewport();
  viewport->SetCamera(FixedCamera(Vec3{0, -6, 2}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.9f);
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}), Opaque(), {-1.2f, 0, 0});
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{0.2f, 0.25f, 0.6f, 1.0f}), Opaque(), {1.2f, 0, 0});
  viewport->Post().Settings().bloom = {.enabled = true, .threshold = 0.7f, .intensity = 1.0f};
  viewport->SetRenderScale(0.5f);
  ui::Canvas& picture = viewport->AddCanvasLayer({.mode = scene::CanvasMode::Picture});
  const float size = gpu_tests::kSize;
  viewport->Internal().BeginCanvases({size, size}, {size, size});
  picture.Circle({128, 52}, 18, Color{1.0f, 0.95f, 0.6f, 1.0f});
  const auto font = ui::Font::Load(WLW_TEST_FONT);
  REQUIRE(font != nullptr);
  AddSharpLabel(viewport->GetCanvas(), *font, "bloom x0.5");
  CheckGolden("render_scale_post", viewport);
  CHECK((viewport->GetRenderStats().render_width == 128 && viewport->GetRenderStats().post_passes == 8 &&
         viewport->GetRenderStats().bloom_levels == 4));  // 4 down, 3 up, the output pass
}

namespace {

// Three cubes casting shadows on a ground plane, all drawn with `material`.
std::shared_ptr<scene::Viewport> ShadowScene(const std::shared_ptr<rendering::Material>& material,
                                             bool ground_receives = true) {
  auto viewport = MakeViewport();
  viewport->SetCamera(FixedCamera(Vec3{0, -6, 5}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.3f)->SetCastsShadows(true);
  auto ground = AddMesh(*viewport, utils::PlaneMesh(8.0f, 8.0f, Color{0.7f, 0.7f, 0.75f, 1.0f}), material, {0, 0, 0});
  ground->SetReceivesShadow(ground_receives);
  for (int i = 0; i < 3; ++i) {
    auto cube = AddMesh(*viewport, utils::CubeMesh(1.0f, Color{0.3f, 0.6f, 0.9f, 1.0f}), material,
                        {-1.5f + 1.5f * i, 0.5f - 0.5f * i, 0.5f + 0.5f * i});
    cube->SetCastsShadow(true);
    cube->SetReceivesShadow(true);
  }
  return viewport;
}

}  // namespace

TEST_CASE("shadows") { CheckGolden("shadows", ShadowScene(Opaque())); }

TEST_CASE("game shader shadows match the engine's") {
  // A mesh shader lit and shadowed like the engine's (SceneLighting + SceneShadow) draws the same
  // picture, and honours SetReceivesShadow. No stored reference: both pictures come from this run.
  const std::shared_ptr<rendering::Shader> shader = rendering::Shader::Create(R"(shader_type mesh;
#include "wlw/lighting.glsl"
#include "wlw/shadows.glsl"
void fragment() {
  vec3 normal = normalize(vNormal);
  float shadow = SceneShadow(vWorldPos, normal);
  FragColor = vec4(vColor.rgb * SceneLighting(vWorldPos, normal, ViewDirection(vWorldPos), 32.0, shadow), 1.0);
}
)");
  REQUIRE(shader->IsValid());
  const std::shared_ptr<rendering::Material> game = rendering::ShaderMaterial::Create(shader);
  SUBCASE("received") {
    gpu_tests::CheckSamePictures("shadows_game_shader", gpu_tests::Render(ShadowScene(Opaque())),
                                 gpu_tests::Render(ShadowScene(game)));
  }
  SUBCASE("not received") {
    gpu_tests::CheckSamePictures("shadows_game_shader_off", gpu_tests::Render(ShadowScene(Opaque(), false)),
                                 gpu_tests::Render(ShadowScene(game, false)));
  }
}

TEST_CASE("instance uniforms") {
  // Nodes sharing one material, each with its own instance params: a node's value wins, else the material's, else 0,
  // and a node without a value never keeps the one of the node drawn before it. A second material on the same shader
  // makes the program switch params (and back on the second draw). Probes, no stored reference.
  const std::shared_ptr<rendering::Shader> shader = rendering::Shader::Create(R"(shader_type mesh;
uniform float uBase;
instance uniform float uFlash;
instance uniform vec2 uTint;
void fragment() { FragColor = vec4(uFlash, uTint.y, uBase, 1.0); }
)",
                                                                              "test_instance");
  REQUIRE((shader->IsValid() && shader->InstanceUniforms().size() == 2));
  auto shared = rendering::ShaderMaterial::Create(shader);
  shared->SetParam("uBase", 1.0f);
  shared->SetParam("uFlash", 0.5f);  // the fallback for nodes without their own
  auto other = rendering::ShaderMaterial::Create(shader);
  other->SetParam("uFlash", 0.25f);
  other->SetParam("uBase", 0.5f);
  std::shared_ptr<rendering::Material> shared_material = std::move(shared);
  std::shared_ptr<rendering::Material> other_material = std::move(other);

  auto viewport = MakeViewport();
  viewport->SetCamera(OrthoCamera(Vec3{0, -10, 0}, Vec3{0, 0, 0}, 10.0f));
  // Left to right; the same depth, so they are drawn in this order within each material.
  const std::array<float, 5> xs{-4.0f, -2.0f, 0.0f, 2.0f, 4.0f};
  auto add = [&](float x, const std::shared_ptr<rendering::Material>& material) {
    return AddMesh(*viewport, utils::CubeMesh(1.0f), material, {x, 0.0f, 0.0f});
  };
  auto set = add(xs[0], shared_material);
  set->SetInstanceParam("uFlash", 1.0f);
  set->SetInstanceParam("uTint", glm::vec2(0.0f, 1.0f));
  add(xs[1], shared_material);  // nothing set: the material's uFlash, uTint 0
  auto other_node = add(xs[2], other_material);
  auto tinted = add(xs[3], shared_material);
  tinted->SetInstanceParam("uTint", glm::vec4(0.0f, 0.5f, 0.0f, 0.0f));  // a vec4 for a vec2: cut
  auto cleared = add(xs[4], shared_material);
  cleared->SetInstanceParam("uFlash", 0.0f);
  cleared->ClearInstanceParam("uFlash");  // back to the material's

  const testing::Image image = gpu_tests::Render(viewport);
  REQUIRE(image.width == gpu_tests::kSize);
  auto probe = [&](float x) {
    const int px = static_cast<int>(std::lround((x / 10.0f + 0.5f) * gpu_tests::kSize));
    const uint8_t* pixel = image.Pixel(px, gpu_tests::kSize / 2);
    return glm::ivec3(pixel[0], pixel[1], pixel[2]);
  };
  auto near = [](glm::ivec3 got, glm::ivec3 want) {
    return glm::all(glm::lessThanEqual(glm::abs(got - want), glm::ivec3(2)));
  };
  CHECK_MESSAGE(near(probe(xs[0]), {255, 255, 255}), "set: " << probe(xs[0]).x << " " << probe(xs[0]).y);
  CHECK_MESSAGE(near(probe(xs[1]), {128, 0, 255}), "unset: " << probe(xs[1]).x << " " << probe(xs[1]).y);
  CHECK_MESSAGE(near(probe(xs[2]), {64, 0, 128}), "other material: " << probe(xs[2]).x << " " << probe(xs[2]).z);
  CHECK_MESSAGE(near(probe(xs[3]), {128, 128, 255}), "tinted: " << probe(xs[3]).x << " " << probe(xs[3]).y);
  CHECK_MESSAGE(near(probe(xs[4]), {128, 0, 255}), "cleared: " << probe(xs[4]).x << " " << probe(xs[4]).y);

  // Drawn again with nothing changed (every upload skipped): the same picture.
  gpu_tests::CheckSamePictures("instance_uniforms_again", image, gpu_tests::Render(viewport));
}

TEST_CASE("transparency") {
  // Two see-through cubes over an opaque one: drawn back to front after it, blended, no depth writes.
  auto viewport = MakeViewport();
  viewport->SetCamera(FixedCamera(Vec3{0, -6, 2}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::CubeMesh(1.5f, Color{0.9f, 0.9f, 0.2f, 1.0f}), Opaque(), {0, 2, 0});
  auto glass = rendering::Material::Create();
  glass->blend = rendering::BlendMode::Alpha;
  std::shared_ptr<rendering::Material> shared_glass = std::move(glass);
  AddMesh(*viewport, utils::CubeMesh(1.5f, Color{1.0f, 0.2f, 0.2f, 0.5f}), shared_glass, {-0.6f, 0, 0});
  AddMesh(*viewport, utils::CubeMesh(1.5f, Color{0.2f, 0.4f, 1.0f, 0.5f}), shared_glass, {0.6f, -1, 0});
  CheckGolden("transparency", viewport);
}

TEST_CASE("instancing") {
  // One node, 25 tinted copies: one draw call.
  auto viewport = MakeViewport();
  viewport->SetCamera(FixedCamera(Vec3{0, -7, 7}, Vec3{0, 0, 0}));
  AddSun(*viewport);
  auto node = AddMesh<scene::MultiMeshInstance3D>(*viewport, utils::CubeMesh(0.7f), Opaque(), {0, 0, 0});
  std::span<rendering::Instance> copies = node->WriteInstances(25);
  for (int z = 0; z < 5; ++z) {
    for (int x = 0; x < 5; ++x) {
      glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x - 2.0f, 2.0f - z, 0.15f * (x + z)));
      transform = glm::rotate(transform, glm::radians(9.0f * (x * 5 + z)), glm::vec3(0, 0, 1));
      const Color tint{0.2f + 0.2f * x, 0.9f - 0.15f * z, 0.5f, 1.0f};
      copies[static_cast<size_t>(z * 5 + x)] = rendering::Instance::FromTransform(transform, tint);
    }
  }
  CheckGolden("instancing", viewport);
}

TEST_CASE("sprites") {
  // A SpriteBatch: soft additive dots in one call, overlapping ones add up.
  auto viewport = MakeViewport();
  // Straight down at the dots on the ground (XY), +Y at the top of the picture (LookRotation's fallback up).
  viewport->SetCamera(OrthoCamera({0, 0, 10}, {0, 0, 0}, 10.0f));
  auto dots = std::make_shared<scene::SpriteBatch>(256);
  viewport->GetScene()->Root().AddChild(dots);
  constexpr int kDots = 120;
  std::span<rendering::SpriteInstance> out = dots->Write(kDots);
  for (int i = 0; i < kDots; ++i) {
    const float t = static_cast<float>(i) / kDots;
    const float angle = t * 6.2831853f * 3.0f;
    const float radius = 0.5f + 3.8f * t;
    out[i].position = {radius * std::cos(angle), radius * std::sin(angle)};
    out[i].size = 0.4f + 1.2f * t;
    out[i].color = wlw::Color32(wlw::Color{1.0f - t, 0.4f + 0.4f * t, t, 0.8f}).Packed();
  }
  dots->FitBounds();
  CheckGolden("sprites", viewport);
}

namespace {

// Six 16x16 faces, one colour each (+X red, -X cyan, +Y green, -Y magenta, +Z blue, -Z yellow), with
// a lighter quarter in each face's first texels: wrong faces or a wrong turn show in the picture.
std::shared_ptr<rendering::WCubemap> TestCubemap() {
  const glm::vec3 colors[6] = {{0.8f, 0.2f, 0.2f}, {0.2f, 0.7f, 0.7f}, {0.2f, 0.7f, 0.3f},
                               {0.7f, 0.2f, 0.7f}, {0.2f, 0.3f, 0.8f}, {0.8f, 0.7f, 0.2f}};
  constexpr int kFace = 16;
  std::array<utils::RawImage, 6> faces;
  for (int face = 0; face < 6; ++face) {
    faces[face].size = {kFace, kFace};
    faces[face].channels = 4;
    faces[face].pixels.resize(kFace * kFace * 4);
    for (int y = 0; y < kFace; ++y) {
      for (int x = 0; x < kFace; ++x) {
        const float light = x < kFace / 2 && y < kFace / 2 ? 1.25f : 1.0f;
        uint8_t* pixel = &faces[face].pixels[(y * kFace + x) * 4];
        for (int c = 0; c < 3; ++c) pixel[c] = static_cast<uint8_t>(std::min(255.0f, colors[face][c] * light * 255.0f));
        pixel[3] = 255;
      }
    }
  }
  return rendering::WCubemap::Create(faces);
}

}  // namespace

TEST_CASE("skybox") {
  // The sky after the opaque cube (only the pixels the cube leaves show it: depth 1.0, LEQUAL) and
  // before the see-through cube, which blends over both. Three faces meet in the corner the camera
  // looks at.
  auto viewport = MakeViewport();
  viewport->SetCamera(FixedCamera(Vec3{-3.0f, 3.0f, 1.5f}, Vec3{0, 0, 0}));
  viewport->GetScene()->SetSkybox(TestCubemap());
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::CubeMesh(1.2f, Color{0.9f, 0.9f, 0.9f, 1.0f}), Opaque(), {0, 0, 0});
  auto glass = rendering::Material::Create();
  glass->blend = rendering::BlendMode::Alpha;
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{1.0f, 1.0f, 1.0f, 0.5f}), std::move(glass), {-0.3f, 0.9f, -0.9f});
  CheckGolden("skybox", viewport);
}

TEST_CASE("skybox orthographic") {
  // An orthographic camera's sky covers the whole viewport too.
  auto viewport = MakeViewport();
  viewport->SetCamera(OrthoCamera({-6.0f, 6.0f, 3.0f}, {0, 0, 0}, 4.0f));
  viewport->GetScene()->SetSkybox(TestCubemap());
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::CubeMesh(1.2f, Color{0.9f, 0.9f, 0.9f, 1.0f}), Opaque(), {0, 0, 0});
  CheckGolden("skybox_ortho", viewport);
}

TEST_CASE("skybox without depth buffer") {
  // A viewport without depth: the sky is drawn first, so the floor is drawn over it.
  std::shared_ptr<scene::Viewport> viewport =
      scene::Viewport::Create({.name = "golden", .depth = false, .clear = Color{0.08f, 0.09f, 0.14f, 1.0f}});
  viewport->SetSize(gpu_tests::kSize, gpu_tests::kSize);
  viewport->SetCamera(FixedCamera(Vec3{0, -4, 4}, Vec3{0, 0, 0}));
  viewport->GetScene()->SetSkybox(TestCubemap());
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::PlaneMesh(3.0f, 3.0f, Color{0.9f, 0.9f, 0.9f, 1.0f}), Opaque(), {0, 0, 0});
  CheckGolden("skybox_no_depth", viewport);
}

TEST_CASE("post chain bloom") {
  // Bright shapes glowing (an 8-bit picture: a threshold below 1): 4 levels down from half size and
  // back up, added in the output pass.
  auto viewport = MakeViewport();
  viewport->SetCamera(FixedCamera(Vec3{0, -6, 2}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.9f);
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}), Opaque(), {-1.2f, 0, 0});
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{0.2f, 0.25f, 0.6f, 1.0f}), Opaque(), {1.2f, 0, 0});
  viewport->Post().Settings().bloom = {.enabled = true, .threshold = 0.7f, .intensity = 1.0f};
  CheckGolden("post_bloom", viewport);
}

namespace {

// An HDR viewport whose whole picture is `color` (its clear colour: a float target keeps values above 1),
// through the output pass with `tone_map`.
testing::Image ToneMapped(Color color, const rendering::ToneMapSettings& tone_map) {
  auto viewport = MakeViewport();
  viewport->SetFormat(rendering::TextureFormat::RGBA16F);
  viewport->SetClearColor(color);
  viewport->Post().Settings().tone_map = tone_map;
  return gpu_tests::Render(viewport);
}

bool NearRgb(const uint8_t* pixel, int r, int g, int b, int tolerance = 2) {
  return std::abs(pixel[0] - r) <= tolerance && std::abs(pixel[1] - g) <= tolerance &&
         std::abs(pixel[2] - b) <= tolerance;
}

}  // namespace

TEST_CASE("tone mapping") {
  // The output pass (post_settings.h) on an HDR orange of (2, 1, 0.5). Clamp cuts each channel at 1: the
  // hue shifts to yellow. Shoulder rolls the peak off to 1 - 0.04 / 1.4 = 0.971 and scales the others with
  // it (0.486, 0.243), then fades towards white by 1 - 1 / (0.15 x 1.029 + 1) = 0.134: (0.971, 0.551, 0.340).
  const Color orange{2.0f, 1.0f, 0.5f, 1.0f};
  const testing::Image clamped = ToneMapped(orange, {.mapper = rendering::ToneMapper::Clamp, .exposure = 1.0001f});
  CHECK(NearRgb(clamped.Pixel(128, 128), 255, 255, 128));
  const testing::Image shoulder = ToneMapped(orange, {.mapper = rendering::ToneMapper::Shoulder});
  CHECK(NearRgb(shoulder.Pixel(128, 128), 248, 140, 87));
  // Below 0.8 Shoulder changes nothing.
  CHECK(NearRgb(ToneMapped({0.6f, 0.4f, 0.2f, 1.0f}, {.mapper = rendering::ToneMapper::Shoulder}).Pixel(10, 10), 153,
                102, 51));
  // Exposure multiplies first.
  CHECK(NearRgb(ToneMapped(orange, {.exposure = 0.5f}).Pixel(200, 50), 255, 128, 64));
}

TEST_CASE("dithering") {
  // 0.5 is half-way between two 8-bit steps (127.5): without dithering every pixel rounds the same way,
  // with it the noise spreads them over both neighbours (no flat bands in gradients).
  auto count_values = [](const testing::Image& image) {
    std::array<int, 256> counts{};
    for (int y = 0; y < image.height; ++y) {
      for (int x = 0; x < image.width; ++x) ++counts[image.Pixel(x, y)[0]];
    }
    return counts;
  };
  const Color grey{0.5f, 0.5f, 0.5f, 1.0f};
  const auto flat = count_values(ToneMapped(grey, {.exposure = 1.0001f}));
  CHECK(std::max(flat[127], flat[128]) == gpu_tests::kSize * gpu_tests::kSize);
  const auto dithered = count_values(ToneMapped(grey, {.dither = true}));
  CHECK((dithered[127] > gpu_tests::kSize * gpu_tests::kSize / 4 &&
         dithered[128] > gpu_tests::kSize * gpu_tests::kSize / 4));
  CHECK(dithered[127] + dithered[128] == gpu_tests::kSize * gpu_tests::kSize);  // never more than half a step
}

namespace {

// An HDR picture that is black but for a 2 x 2 square of `value` at pixel `corner` (drawn by a post
// pass, so it is exact to the pixel), through bloom (Clamp tone mapping: the glow adds on unchanged).
testing::Image BloomedSquare(glm::vec2 corner, float value, const rendering::BloomSettings& bloom) {
  static const char* kSquare = R"(shader_type post;
uniform vec2 uCorner;
uniform float uValue;
void fragment() {
  vec2 p = vUV * uOutputSize - uCorner;
  bool inside = p.x >= 0.0 && p.y >= 0.0 && p.x < 2.0 && p.y < 2.0;
  FragColor = vec4(vec3(inside ? uValue : 0.0), 1.0);
})";
  auto viewport = MakeViewport();
  viewport->SetScene(nullptr);
  viewport->SetFormat(rendering::TextureFormat::RGBA16F);
  std::shared_ptr<rendering::ShaderMaterial> square =
      rendering::ShaderMaterial::Create(rendering::Shader::Create(kSquare, "test_square"));
  square->SetParam("uCorner", corner);
  square->SetParam("uValue", value);
  viewport->AddPostPass({.material = square});
  viewport->Post().Settings().bloom = bloom;
  return gpu_tests::Render(viewport);
}

// The glow alone: the bloomed picture minus the same picture without bloom, summed over every
// pixel (0..255 each, red only: it is grey).
double GlowSum(glm::vec2 corner, float value, const rendering::BloomSettings& bloom) {
  const testing::Image bloomed = BloomedSquare(corner, value, bloom);
  const testing::Image plain = BloomedSquare(corner, value, {});
  double sum = 0.0;
  for (int y = 0; y < bloomed.height; ++y) {
    for (int x = 0; x < bloomed.width; ++x) sum += bloomed.Pixel(x, y)[0] - plain.Pixel(x, y)[0];
  }
  return sum;
}

}  // namespace

TEST_CASE("bloom probes") {
  // Stability: a 2-pixel spark moved by a pixel (across, down, both, further) glows as much. Each
  // level's 13 taps see every texel of what they cover, so no position falls between them. The old
  // bloom (one bilinear read per quarter-size texel: 4 of its 16 texels) went from full glow to none.
  const std::array<glm::vec2, 4> moves{glm::vec2(128, 127), glm::vec2(127, 128), glm::vec2(128, 128),
                                       glm::vec2(130, 132)};
  auto check_stable = [&moves](const rendering::BloomSettings& bloom, double tolerance) {
    const double still = GlowSum({127, 127}, 8.0f, bloom);
    CHECK(still > 1000.0);
    for (const glm::vec2 corner : moves) {
      const double moved = GlowSum(corner, 8.0f, bloom);
      CHECK_MESSAGE(std::abs(moved - still) < tolerance * still,
                    "moved to " << corner.x << ", " << corner.y << ": " << moved << " vs " << still);
    }
  };
  // The filters are linear, but the first level's Karis average (each box weighed by 1 / (1 + its peak))
  // and threshold (per tap: a spark split between taps loses more to it) aren't. Without threshold:
  // within 10% (measured 9%; under 5% with Karis off). With it: within 20% (measured 12%). Worth it: no
  // fireflies, and nothing below the threshold glows.
  check_stable({.enabled = true, .threshold = 0.0f, .knee = 0.0f, .intensity = 1.0f}, 0.1);
  const rendering::BloomSettings bloom{.enabled = true, .threshold = 1.0f, .knee = 0.0f, .intensity = 1.0f};
  check_stable(bloom, 0.2);

  // Below the threshold: no glow at all.
  CHECK(GlowSum({127, 127}, 0.9f, bloom) == 0.0);

  // The glow falls off with distance, along the square's rows (127 and 128, either way up).
  const testing::Image image = BloomedSquare({127, 127}, 8.0f, bloom);
  const int near = image.Pixel(131, 127)[0], middle = image.Pixel(140, 127)[0], far = image.Pixel(170, 127)[0];
  CHECK_MESSAGE((near > middle && middle > far), near << " " << middle << " " << far);
  CHECK(image.Pixel(250, 5)[0] == 0);  // a corner far away: nothing
}

TEST_CASE("canvas") {
  // The 2D layer alone: shapes, text from the SDF atlas, images (straight alpha, additive).
  auto viewport = MakeViewport();
  viewport->SetScene(nullptr);
  const float size = gpu_tests::kSize;
  viewport->Internal().BeginCanvases({size, size}, {size, size});
  ui::Canvas& canvas = viewport->GetCanvas();
  canvas.FillRect({{12, 12}, {244, 92}}, Color{0.95f, 0.55f, 0.2f, 1.0f}, 18.0f);
  canvas.FillRect({{24, 104}, {120, 200}}, Color{0.2f, 0.7f, 0.5f, 0.6f});
  canvas.Circle({170, 150}, 40, Color{0.3f, 0.5f, 0.95f, 1.0f});
  canvas.Ring({170, 150}, 52, 6, Color{1.0f, 1.0f, 1.0f, 0.8f});
  canvas.Triangle({30, 244}, {90, 210}, {110, 250}, Color{0.9f, 0.2f, 0.4f, 1.0f});
  canvas.Line({130, 240}, {240, 205}, 4.0f, Color{0.9f, 0.9f, 0.3f, 1.0f});
  canvas.Arc({60, 150}, 26, 8, 0.5f, 3.5f, Color{1.0f, 0.9f, 0.9f, 1.0f});
  const std::shared_ptr<rendering::WTexture2D> glow = utils::RadialFalloffTexture(64);
  canvas.Image({{150, 180}, {250, 256}}, glow,
               {.tint = {1.0f, 0.5f, 0.1f, 1.0f}, .blend = rendering::BlendMode::Additive});
  const auto font = ui::Font::Load(WLW_TEST_FONT);
  REQUIRE(font != nullptr);
  canvas.Text(*font, "Golden 0.3", {128, 52}, 34, ui::Align::Center,
              {.color = {1, 1, 1, 1}, .outline = 0.06f, .shadow_offset = {2, 2}});
  CheckGolden("canvas", viewport);
}

TEST_CASE("canvas gradient fades without darkening") {
  // Red to transparent black: halfway is red at half alpha (premultiplied blending), the same as a flat rect of it,
  // not the darker colour straight-alpha blending gives (half the red at half alpha).
  auto viewport = MakeViewport();
  viewport->SetScene(nullptr);
  const float size = gpu_tests::kSize;
  viewport->Internal().BeginCanvases({size, size}, {size, size});
  ui::Canvas& canvas = viewport->GetCanvas();
  canvas.FillRect({{0, 0}, {size, size * 0.5f}},
                  ui::LinearGradient::Horizontal({{0.0f, Color{1, 0, 0, 1}}, {1.0f, Color{0, 0, 0, 0}}}));
  canvas.FillRect({{0, size * 0.5f}, {size, size}}, Color{1, 0, 0, 0.5f});
  const testing::Image image = gpu_tests::Render(viewport);
  const int x = static_cast<int>(size * 0.5f);
  const int gradient = image.Pixel(x, static_cast<int>(size * 0.25f))[0];
  const int flat = image.Pixel(x, static_cast<int>(size * 0.75f))[0];
  CHECK_MESSAGE(std::abs(gradient - flat) <= 3, gradient << " vs " << flat);
  CHECK(image.Pixel(4, static_cast<int>(size * 0.25f))[0] > gradient);  // brighter near the opaque end
}

namespace {

// Nodes whose transforms turn normals in every way the renderer handles: an uneven scale, a mirror,
// a parent's uneven scale under a child's rotation (shear), and instanced copies squashed and mirrored
// under a scaled node. Returns the nodes that draw.
std::vector<std::shared_ptr<scene::GeometryInstance3D>> AddTransformedShapes(
    scene::Viewport& viewport, const std::shared_ptr<rendering::Material>& material) {
  std::vector<std::shared_ptr<scene::GeometryInstance3D>> drawn;
  auto place = [&](const std::shared_ptr<scene::GeometryInstance3D>& node, const Vec3& rotation, const Vec3& scale) {
    node->SetEulerAngles(glm::radians(rotation), wlw::EulerOrder::XZY);
    node->SetScale(scale);
    drawn.push_back(node);
  };
  auto add = [&](std::shared_ptr<core::Mesh<core::Vertex3D>> mesh, const Vec3& position, const Vec3& rotation,
                 const Vec3& scale) { place(AddMesh(viewport, std::move(mesh), material, position), rotation, scale); };
  add(utils::SphereMesh(0.7f, 16, 32), {-1.5f, 0, 1.1f}, {0, -25, 0}, {1.6f, 1.0f, 0.6f});
  add(utils::BeveledCubeMesh(), {-1.5f, 0, -1.1f}, {20, -10, 45}, {-1.0f, 1.2f, 0.8f});

  auto parent = std::make_shared<scene::Node3D>();
  parent->SetPosition({1.5f, 0, 1.1f});
  parent->SetEulerAngles(glm::radians(wlw::Vec3(0, -40, 0)), wlw::EulerOrder::XZY);
  parent->SetScale({1.7f, 1.0f, 0.6f});
  viewport.GetScene()->Root().AddChild(parent);
  auto child = std::make_shared<scene::MeshInstance3D>();
  child->SetModel(MakeModel(utils::CubeMesh(0.9f, Color{0.3f, 0.8f, 0.4f, 1.0f})));
  child->SetMaterial(material);
  child->SetEulerAngles(glm::radians(wlw::Vec3(30, 0, 35)), wlw::EulerOrder::XZY);
  parent->AddChild(child);
  drawn.push_back(child);

  auto copies =
      AddMesh<scene::MultiMeshInstance3D>(viewport, utils::SphereMesh(0.4f, 12, 24), material, {1.5f, 0, -1.1f});
  place(copies, {0, 0, 0}, {1.3f, 1.0f, 1.0f});
  std::span<rendering::Instance> instances = copies->WriteInstances(3);
  instances[0].SetTransform(glm::scale(glm::translate(glm::mat4(1.0f), {-0.7f, 0, 0}), {1.0f, 1.0f, 0.4f}));
  instances[1].SetTransform(glm::scale(glm::rotate(glm::mat4(1.0f), 0.6f, {0, -1, 0}), {-0.8f, 1.0f, 1.2f}));
  instances[2].SetTransform(
      glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), {0.7f, 0, 0}), 0.9f, {1, 0, 1}), {0.5f, 1.5f, -1.0f}));
  return drawn;
}

// The same shapes with every transform baked into the vertices on the CPU, normals with the textbook
// transpose(inverse(m)): what the renderer's normals must match.
void AddBakedShapes(scene::Viewport& viewport, const std::vector<std::shared_ptr<scene::GeometryInstance3D>>& shapes) {
  auto bake = [&](const core::Mesh<core::Vertex3D>& mesh, const std::shared_ptr<rendering::Material>& material,
                  const glm::mat4& world) {
    const glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(world)));
    std::vector<core::Vertex3D> vertices = mesh.GetVertices();
    for (core::Vertex3D& v : vertices) {
      const glm::vec3 p = world * glm::vec4(v.position.x, v.position.y, v.position.z, 1.0f);
      const glm::vec3 n = glm::normalize(normal_matrix * glm::vec3(v.normal.x, v.normal.y, v.normal.z));
      v.position = {p.x, p.y, p.z};
      v.normal = {n.x, n.y, n.z};
    }
    auto baked = std::make_shared<core::Mesh<core::Vertex3D>>();
    baked->SetVertices(vertices);
    baked->SetIndices(mesh.GetIndices());
    AddMesh(viewport, baked, material, {0, 0, 0});
  };
  for (const auto& shape : shapes) {
    const glm::mat4 world = shape->GetWorldMatrix();
    for (const auto& mesh : shape->GetModel()->meshes) {
      if (shape->Kind() != scene::NodeKind::MultiMeshInstance) {
        bake(*mesh, shape->GetMaterial(), world);
        continue;
      }
      for (const auto& copy : static_cast<const scene::MultiMeshInstance3D&>(*shape).Instances()) {
        bake(*mesh, shape->GetMaterial(), world * copy.Transform());
      }
    }
  }
}

// Draws the shapes twice, moved by their transforms and baked, lit by a sun and a point light.
void CheckNormalsMatchBaked(const std::string& name, const std::shared_ptr<rendering::Material>& material) {
  auto light = [](scene::Viewport& viewport) {
    viewport.SetCamera(FixedCamera(Vec3{0, -8, 0.5f}, Vec3{0, 0, 0}));
    AddSun(viewport);
    auto point = std::make_shared<scene::PointLight3D>();
    point->SetPosition({0.0f, -2.5f, 0.0f});
    point->SetColor({1.0f, 0.6f, 0.2f});
    point->SetRange(6.0f);
    viewport.GetScene()->Root().AddChild(point);
  };
  auto transformed = MakeViewport();
  light(*transformed);
  const auto shapes = AddTransformedShapes(*transformed, material);
  auto baked = MakeViewport();
  light(*baked);
  AddBakedShapes(*baked, shapes);
  gpu_tests::CheckSamePictures(name, gpu_tests::Render(transformed), gpu_tests::Render(baked));
}

}  // namespace

TEST_CASE("normals match inverse-transpose") {
  // The renderer's normal matrices (core::NormalMatrix on the CPU, NormalMatrix() per instanced copy)
  // light the shapes like the textbook transpose(inverse(m)) does. Mirrored ones too: a normal turned
  // into the surface would light the wrong faces. No stored reference: both pictures come from this run.
  auto lit = Opaque();
  lit->shininess = 32.0f;
  SUBCASE("engine shader") { CheckNormalsMatchBaked("normals_lit", lit); }
  SUBCASE("game shader") {
    // A mesh shader without vertex() (the engine's WorldNormal), showing the normal as a colour.
    const std::shared_ptr<rendering::Shader> shader = rendering::Shader::Create(R"(shader_type mesh;
void fragment() {
  FragColor = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
}
)");
    REQUIRE(shader->IsValid());
    CheckNormalsMatchBaked("normals_game_shader", rendering::ShaderMaterial::Create(shader));
  }
}

// ---- Conventions (core/math.h): probes of a few pixels, no stored reference ----

namespace {

// Which of `colors` (by hue: each normalized by its largest channel) the pixel is closest to.
int NearestColor(const uint8_t* pixel, std::span<const glm::vec3> colors) {
  const glm::vec3 p(pixel[0], pixel[1], pixel[2]);
  const glm::vec3 hue = p / std::max({p.r, p.g, p.b, 1.0f});
  int best = -1;
  float best_distance = 1e9f;
  for (size_t i = 0; i < colors.size(); ++i) {
    const glm::vec3 c = colors[i] / std::max({colors[i].r, colors[i].g, colors[i].b});
    const float distance = glm::length(hue - c);
    if (distance < best_distance) {
      best_distance = distance;
      best = static_cast<int>(i);
    }
  }
  return best;
}

}  // namespace

TEST_CASE("axes") {
  // A camera at the origin looking along +Y: a blue cube ahead, a red one to the right (+X) and a green one above
  // (+Z) show where they should on screen, and a white one behind (-Y) is not seen.
  auto viewport = MakeViewport();
  auto camera = scene::Camera3D::Create();
  camera->SetPerspective(glm::radians(60.0f), 0.1f, 100.0f);
  viewport->SetCamera(std::move(camera));
  AddSun(*viewport, 1.0f);
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{0.1f, 0.1f, 0.9f, 1.0f}), Opaque(), {0, 6, 0});
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{0.9f, 0.1f, 0.1f, 1.0f}), Opaque(), {2, 6, 0});
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{0.1f, 0.9f, 0.1f, 1.0f}), Opaque(), {0, 6, 2});
  AddMesh(*viewport, utils::CubeMesh(1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}), Opaque(), {0, -6, 0});
  const testing::Image image = gpu_tests::Render(viewport);
  REQUIRE(!image.Empty());
  // 6 m away the view is 2 * 6 * tan(30) = 6.93 m tall: 2 m off the middle is 74 pixels.
  const glm::vec3 colors[] = {{0.1f, 0.1f, 0.9f}, {0.9f, 0.1f, 0.1f}, {0.1f, 0.9f, 0.1f}};
  CHECK(NearestColor(image.Pixel(128, 128), colors) == 0);  // ahead: the middle
  CHECK(NearestColor(image.Pixel(202, 128), colors) == 1);  // +X: right
  CHECK(NearestColor(image.Pixel(128, 54), colors) == 2);   // +Z: up (the picture's top row is row 0)
  CHECK(*std::max_element(image.Pixel(54, 128), image.Pixel(54, 128) + 3) < 60);  // nothing on the left (-X)
  // The cube behind the camera is nowhere.
  int white = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) white += image.Pixel(x, y)[0] > 200 && image.Pixel(x, y)[2] > 200;
  }
  CHECK(white == 0);
}

TEST_CASE("cube faces") {
  // The skybox keeps a cube map's usual Y-up layout: its +Y face is the sky above (world +Z), its -Z face is ahead
  // (world +Y), +X stays +X. A camera at the origin looks each way and finds that face's colour.
  const glm::vec3 colors[6] = {{0.8f, 0.2f, 0.2f}, {0.2f, 0.7f, 0.7f}, {0.2f, 0.7f, 0.3f},
                               {0.7f, 0.2f, 0.7f}, {0.2f, 0.3f, 0.8f}, {0.8f, 0.7f, 0.2f}};  // TestCubemap's
  struct Look {
    Vec3 direction;
    int face;  // +X, -X, +Y, -Y, +Z, -Z
  };
  const Look looks[] = {{kRight, 0}, {kLeft, 1}, {kUp, 2}, {kDown, 3}, {kBack, 4}, {kForward, 5}};
  for (const Look& look : looks) {
    CAPTURE(look.face);
    auto viewport = MakeViewport();
    viewport->GetScene()->SetSkybox(TestCubemap());
    auto camera = scene::Camera3D::Create();
    camera->SetPerspective(glm::radians(60.0f), 0.1f, 100.0f);
    camera->LookAt(look.direction);
    viewport->SetCamera(std::move(camera));
    const testing::Image image = gpu_tests::Render(viewport);
    REQUIRE(!image.Empty());
    CHECK(NearestColor(image.Pixel(100, 100), colors) == look.face);
  }
}

TEST_SUITE_END();
