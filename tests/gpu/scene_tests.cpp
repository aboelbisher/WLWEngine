// Golden-image scenes: one per renderer feature (golden.h). Everything is fixed: no time, no random
// numbers, fixed cameras, so the same GPU draws the same pixels every run.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>
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
#include "wlw/scene/light.h"
#include "wlw/scene/node.h"
#include "wlw/scene/orthographic_camera.h"
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

// A perspective camera that never moves (45 degrees, looking from `eye` at `target`).
class FixedCamera : public scene::Camera3D {
 public:
  FixedCamera(const Vec3& eye, const Vec3& target) : eye_(eye), target_(target) { Update(); }
  void SetPosition(const Vec3& position) override { eye_ = position, Update(); }
  void SetTarget(const Vec3& target) override { target_ = target, Update(); }
  void UpdateSize(const Vec2& size) override {
    projection_ = glm::perspective(glm::radians(45.0f), size.x / size.y, 0.1f, 100.0f);
  }
  const Vec3& GetPosition() const override { return eye_; }
  const glm::mat4& GetViewMatrix() override { return view_; }
  const glm::mat4& GetProjectionMatrix() override { return projection_; }
  void ProcessKeyboard(scene::CameraMovement, float) override {}
  void ProcessMouseMovement(float, float, bool) override {}

 private:
  void Update() {
    view_ = glm::lookAt(glm::vec3(eye_.x, eye_.y, eye_.z), glm::vec3(target_.x, target_.y, target_.z),
                        glm::vec3(0.0f, 1.0f, 0.0f));
  }
  Vec3 eye_, target_;
  glm::mat4 view_{1.0f};
  glm::mat4 projection_ = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
};

using Model = core::Model<core::Vertex3D>;

std::shared_ptr<Model> MakeModel(std::shared_ptr<core::Mesh<core::Vertex3D>> mesh) {
  auto model = std::make_shared<Model>();
  model->meshes.push_back(std::move(mesh));
  return model;
}

std::shared_ptr<scene::Node3D> AddMesh(scene::Viewport& viewport, std::shared_ptr<core::Mesh<core::Vertex3D>> mesh,
                                       std::shared_ptr<rendering::Material> material, const Vec3& position) {
  auto node = std::make_shared<scene::Node3D>();
  node->SetModel(MakeModel(std::move(mesh)));
  node->SetMaterial(std::move(material));
  node->SetPosition(position);
  viewport.GetScene()->Root().AddChild(node);
  return node;
}

std::shared_ptr<scene::DirectionalLight> AddSun(scene::Viewport& viewport, float ambient = 0.25f) {
  auto sun = std::make_shared<scene::DirectionalLight>();
  sun->SetDirection({-0.4f, -1.0f, -0.6f});
  sun->SetAmbient(ambient);
  viewport.GetScene()->Root().AddChild(sun);
  return sun;
}

std::shared_ptr<rendering::Material> Opaque() { return rendering::Material::Create(); }

// A cube, a bevelled cube and a sphere under a sun and an orange point light.
void AddLitCubes(scene::Viewport& viewport) {
  viewport.SetCamera(std::make_shared<FixedCamera>(Vec3{0, 3, 7}, Vec3{0, 0, 0}));
  AddSun(viewport);
  auto point = std::make_shared<scene::PointLight>();
  point->SetPosition({2.0f, 1.5f, 2.0f});
  point->SetColor({1.0f, 0.6f, 0.2f});
  point->SetRange(6.0f);
  viewport.GetScene()->Root().AddChild(point);
  auto material = Opaque();
  material->shininess = 32.0f;
  AddMesh(viewport, utils::createCubeWithNormals(1.2f, Color{0.9f, 0.3f, 0.3f, 1.0f}), material, {-1.8f, 0, 0})
      ->SetEulerAngles(glm::radians(wlw::Vec3(0, 30, 0)), wlw::EulerOrder::XYZ);
  AddMesh(viewport, utils::createBeveledCubeWithNormals(), material, {0, 0, 0})
      ->SetEulerAngles(glm::radians(wlw::Vec3(20, 45, 0)), wlw::EulerOrder::XYZ);
  AddMesh(viewport, utils::createSphereWithNormals(0.7f, 16, 32), material, {1.8f, 0, 0});
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
  viewport->BeginCanvases({size, size}, {size, size});
  const auto font = ui::Font::Load(WLW_TEST_FONT);
  REQUIRE(font != nullptr);
  AddSharpLabel(viewport->GetCanvas(), *font, "x0.5");
  CheckGolden("render_scale", viewport);
  CHECK((viewport->GetRenderStats().render_width == 128 && viewport->GetRenderStats().post_passes == 1));
}

TEST_CASE("render scale with post") {
  // Bloom on a picture drawn at 0.5: its passes keep their sizes (a quarter of the viewport's) and
  // the last one writes the full-size picture (no extra copy). A Picture layer is drawn into the
  // small picture (bloomed, soft); the Overlay layer stays sharp.
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(Vec3{0, 2, 6}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.9f);
  AddMesh(*viewport, utils::createCubeWithNormals(1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}), Opaque(), {-1.2f, 0, 0});
  AddMesh(*viewport, utils::createCubeWithNormals(1.0f, Color{0.2f, 0.25f, 0.6f, 1.0f}), Opaque(), {1.2f, 0, 0});
  scene::AddBloom(*viewport, {.threshold = 0.7f, .strength = 1.0f});
  viewport->SetRenderScale(0.5f);
  ui::Canvas& picture = viewport->AddCanvasLayer({.mode = scene::CanvasMode::Picture});
  const float size = gpu_tests::kSize;
  viewport->BeginCanvases({size, size}, {size, size});
  picture.Circle({128, 52}, 18, Color{1.0f, 0.95f, 0.6f, 1.0f});
  const auto font = ui::Font::Load(WLW_TEST_FONT);
  REQUIRE(font != nullptr);
  AddSharpLabel(viewport->GetCanvas(), *font, "bloom x0.5");
  CheckGolden("render_scale_post", viewport);
  CHECK((viewport->GetRenderStats().render_width == 128 && viewport->GetRenderStats().post_passes == 6));
}

TEST_CASE("shadows") {
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(Vec3{0, 5, 6}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.3f)->SetCastsShadows(true);
  auto ground = AddMesh(*viewport, utils::createPlaneWithNormals(8.0f, 8.0f, Color{0.7f, 0.7f, 0.75f, 1.0f}), Opaque(),
                        {0, 0, 0});
  ground->SetReceivesShadow(true);
  for (int i = 0; i < 3; ++i) {
    auto cube = AddMesh(*viewport, utils::createCubeWithNormals(1.0f, Color{0.3f, 0.6f, 0.9f, 1.0f}), Opaque(),
                        {-1.5f + 1.5f * i, 0.5f + 0.5f * i, -0.5f + 0.5f * i});
    cube->SetCastsShadow(true);
    cube->SetReceivesShadow(true);
  }
  CheckGolden("shadows", viewport);
}

TEST_CASE("transparency") {
  // Two see-through cubes over an opaque one: drawn back to front after it, blended, no depth writes.
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(Vec3{0, 2, 6}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::createCubeWithNormals(1.5f, Color{0.9f, 0.9f, 0.2f, 1.0f}), Opaque(), {0, 0, -2});
  auto glass = rendering::Material::Create();
  glass->blend = rendering::BlendMode::Alpha;
  std::shared_ptr<rendering::Material> shared_glass = std::move(glass);
  AddMesh(*viewport, utils::createCubeWithNormals(1.5f, Color{1.0f, 0.2f, 0.2f, 0.5f}), shared_glass, {-0.6f, 0, 0});
  AddMesh(*viewport, utils::createCubeWithNormals(1.5f, Color{0.2f, 0.4f, 1.0f, 0.5f}), shared_glass, {0.6f, 0, 1});
  CheckGolden("transparency", viewport);
}

TEST_CASE("instancing") {
  // One node, 25 tinted copies: one draw call.
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(Vec3{0, 7, 7}, Vec3{0, 0, 0}));
  AddSun(*viewport);
  auto node = AddMesh(*viewport, utils::createCubeWithNormals(0.7f), Opaque(), {0, 0, 0});
  std::span<rendering::Instance> copies = node->WriteInstances(25);
  for (int z = 0; z < 5; ++z) {
    for (int x = 0; x < 5; ++x) {
      glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x - 2.0f, 0.15f * (x + z), z - 2.0f));
      transform = glm::rotate(transform, glm::radians(9.0f * (x * 5 + z)), glm::vec3(0, 1, 0));
      const Color tint{0.2f + 0.2f * x, 0.9f - 0.15f * z, 0.5f, 1.0f};
      copies[static_cast<size_t>(z * 5 + x)] = rendering::Instance::FromTransform(transform, tint);
    }
  }
  CheckGolden("instancing", viewport);
}

TEST_CASE("sprites") {
  // A SpriteBatch: soft additive dots in one call, overlapping ones add up.
  auto viewport = MakeViewport();
  auto camera = scene::OrthographicCamera::Create();
  camera->SetPosition({0, 0, 10});
  camera->SetTarget({0, 0, 0});
  camera->SetVisibleArea(10.0f, 10.0f);
  camera->SetClipPlanes(0.1f, 50.0f);
  viewport->SetCamera(camera);
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
  viewport->SetCamera(std::make_shared<FixedCamera>(Vec3{-3.0f, 1.5f, -3.0f}, Vec3{0, 0, 0}));
  viewport->GetScene()->SetSkybox(TestCubemap());
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::createCubeWithNormals(1.2f, Color{0.9f, 0.9f, 0.9f, 1.0f}), Opaque(), {0, 0, 0});
  auto glass = rendering::Material::Create();
  glass->blend = rendering::BlendMode::Alpha;
  AddMesh(*viewport, utils::createCubeWithNormals(1.0f, Color{1.0f, 1.0f, 1.0f, 0.5f}), std::move(glass),
          {-0.3f, -0.9f, -0.9f});
  CheckGolden("skybox", viewport);
}

TEST_CASE("skybox orthographic") {
  // An orthographic camera's sky covers the whole viewport too.
  auto viewport = MakeViewport();
  auto camera = scene::OrthographicCamera::Create();
  camera->SetPosition({-6.0f, 3.0f, -6.0f});
  camera->SetTarget({0, 0, 0});
  camera->SetVisibleArea(4.0f, 4.0f);
  camera->SetClipPlanes(0.1f, 50.0f);
  viewport->SetCamera(camera);
  viewport->GetScene()->SetSkybox(TestCubemap());
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::createCubeWithNormals(1.2f, Color{0.9f, 0.9f, 0.9f, 1.0f}), Opaque(), {0, 0, 0});
  CheckGolden("skybox_ortho", viewport);
}

TEST_CASE("skybox without depth buffer") {
  // A viewport without depth: the sky is drawn first, so the floor is drawn over it.
  auto viewport =
      scene::Viewport::Create({.name = "golden", .depth = false, .clear = Color{0.08f, 0.09f, 0.14f, 1.0f}});
  viewport->SetSize(gpu_tests::kSize, gpu_tests::kSize);
  viewport->SetCamera(std::make_shared<FixedCamera>(Vec3{0, 4, 4}, Vec3{0, 0, 0}));
  viewport->GetScene()->SetSkybox(TestCubemap());
  AddSun(*viewport, 0.4f);
  AddMesh(*viewport, utils::createPlaneWithNormals(3.0f, 3.0f, Color{0.9f, 0.9f, 0.9f, 1.0f}), Opaque(), {0, 0, 0});
  CheckGolden("skybox_no_depth", viewport);
}

TEST_CASE("post chain bloom") {
  // Bright shapes glowing: bright pass, blur at a quarter size, combined onto the picture.
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(Vec3{0, 2, 6}, Vec3{0, 0, 0}));
  AddSun(*viewport, 0.9f);
  AddMesh(*viewport, utils::createCubeWithNormals(1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}), Opaque(), {-1.2f, 0, 0});
  AddMesh(*viewport, utils::createCubeWithNormals(1.0f, Color{0.2f, 0.25f, 0.6f, 1.0f}), Opaque(), {1.2f, 0, 0});
  scene::AddBloom(*viewport, {.threshold = 0.7f, .strength = 1.0f});
  CheckGolden("post_bloom", viewport);
}

TEST_CASE("canvas") {
  // The 2D layer alone: shapes, text from the SDF atlas, images (straight alpha, additive).
  auto viewport = MakeViewport();
  viewport->SetScene(nullptr);
  const float size = gpu_tests::kSize;
  viewport->BeginCanvases({size, size}, {size, size});
  ui::Canvas& canvas = viewport->GetCanvas();
  canvas.FillRect({{12, 12}, {244, 92}}, Color{0.95f, 0.55f, 0.2f, 1.0f}, 18.0f);
  canvas.FillRect({{24, 104}, {120, 200}}, Color{0.2f, 0.7f, 0.5f, 0.6f});
  canvas.Circle({170, 150}, 40, Color{0.3f, 0.5f, 0.95f, 1.0f});
  canvas.Ring({170, 150}, 52, 6, Color{1.0f, 1.0f, 1.0f, 0.8f});
  canvas.Triangle({30, 244}, {90, 210}, {110, 250}, Color{0.9f, 0.2f, 0.4f, 1.0f});
  canvas.Line({130, 240}, {240, 205}, 4.0f, Color{0.9f, 0.9f, 0.3f, 1.0f});
  canvas.Arc({60, 150}, 26, 8, 0.5f, 3.5f, Color{1.0f, 0.9f, 0.9f, 1.0f});
  const auto glow = utils::RadialFalloffTexture(64);
  canvas.Image({{150, 180}, {250, 256}}, glow,
               {.tint = {1.0f, 0.5f, 0.1f, 1.0f}, .blend = rendering::BlendMode::Additive});
  const auto font = ui::Font::Load(WLW_TEST_FONT);
  REQUIRE(font != nullptr);
  canvas.Text(*font, "Golden 0.3", {128, 52}, 34, ui::Align::Center,
              {.color = {1, 1, 1, 1}, .outline = 0.06f, .shadow_offset = {2, 2}});
  CheckGolden("canvas", viewport);
}

namespace {

// Nodes whose transforms turn normals in every way the renderer handles: an uneven scale, a mirror,
// a parent's uneven scale under a child's rotation (shear), and instanced copies squashed and mirrored
// under a scaled node. Returns the nodes that draw.
std::vector<std::shared_ptr<scene::Node3D>> AddTransformedShapes(scene::Viewport& viewport,
                                                                 const std::shared_ptr<rendering::Material>& material) {
  std::vector<std::shared_ptr<scene::Node3D>> drawn;
  auto add = [&](std::shared_ptr<core::Mesh<core::Vertex3D>> mesh, const Vec3& position, const Vec3& rotation,
                 const Vec3& scale) {
    auto node = AddMesh(viewport, std::move(mesh), material, position);
    node->SetEulerAngles(glm::radians(rotation), wlw::EulerOrder::XYZ);
    node->SetScale(scale);
    drawn.push_back(node);
    return node;
  };
  add(utils::createSphereWithNormals(0.7f, 16, 32), {-1.5f, 1.1f, 0}, {0, 0, 25}, {1.6f, 0.6f, 1.0f});
  add(utils::createBeveledCubeWithNormals(), {-1.5f, -1.1f, 0}, {20, 45, 10}, {-1.0f, 0.8f, 1.2f});

  auto parent = std::make_shared<scene::Node3D>();
  parent->SetPosition({1.5f, 1.1f, 0});
  parent->SetEulerAngles(glm::radians(wlw::Vec3(0, 0, 40)), wlw::EulerOrder::XYZ);
  parent->SetScale({1.7f, 0.6f, 1.0f});
  viewport.GetScene()->Root().AddChild(parent);
  auto child = std::make_shared<scene::Node3D>();
  child->SetModel(MakeModel(utils::createCubeWithNormals(0.9f, Color{0.3f, 0.8f, 0.4f, 1.0f})));
  child->SetMaterial(material);
  child->SetEulerAngles(glm::radians(wlw::Vec3(30, 35, 0)), wlw::EulerOrder::XYZ);
  parent->AddChild(child);
  drawn.push_back(child);

  auto copies = add(utils::createSphereWithNormals(0.4f, 12, 24), {1.5f, -1.1f, 0}, {0, 0, 0}, {1.3f, 1.0f, 1.0f});
  std::span<rendering::Instance> instances = copies->WriteInstances(3);
  instances[0].SetTransform(glm::scale(glm::translate(glm::mat4(1.0f), {-0.7f, 0, 0}), {1.0f, 0.4f, 1.0f}));
  instances[1].SetTransform(glm::scale(glm::rotate(glm::mat4(1.0f), 0.6f, {0, 0, 1}), {-0.8f, 1.2f, 1.0f}));
  instances[2].SetTransform(
      glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), {0.7f, 0, 0}), 0.9f, {1, 1, 0}), {0.5f, -1.0f, 1.5f}));
  return drawn;
}

// The same shapes with every transform baked into the vertices on the CPU, normals with the textbook
// transpose(inverse(m)): what the renderer's normals must match.
void AddBakedShapes(scene::Viewport& viewport, const std::vector<std::shared_ptr<scene::Node3D>>& shapes) {
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
      if (!shape->IsInstanced()) bake(*mesh, shape->GetMaterial(), world);
      for (const auto& copy : shape->Instances()) bake(*mesh, shape->GetMaterial(), world * copy.Transform());
    }
  }
}

// Draws the shapes twice, moved by their transforms and baked, lit by a sun and a point light.
void CheckNormalsMatchBaked(const std::string& name, const std::shared_ptr<rendering::Material>& material) {
  auto light = [](scene::Viewport& viewport) {
    viewport.SetCamera(std::make_shared<FixedCamera>(Vec3{0, 0.5f, 8}, Vec3{0, 0, 0}));
    AddSun(viewport);
    auto point = std::make_shared<scene::PointLight>();
    point->SetPosition({0.0f, 0.0f, 2.5f});
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
    const auto shader = rendering::Shader::Create(R"(shader_type mesh;
void fragment() {
  FragColor = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);
}
)");
    REQUIRE(shader->IsValid());
    CheckNormalsMatchBaked("normals_game_shader", rendering::ShaderMaterial::Create(shader));
  }
}

TEST_SUITE_END();
