// Golden-image scenes: one per renderer feature (golden.h). Everything is fixed: no time, no random
// numbers, fixed cameras, so the same GPU draws the same pixels every run.
#include <cmath>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "golden.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/material.h"
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
using core::Color;
using gpu_tests::CheckGolden;
using gpu_tests::MakeViewport;

namespace {

// A perspective camera that never moves (45 degrees, looking from `eye` at `target`).
class FixedCamera : public scene::Camera3D {
 public:
  FixedCamera(const core::Vector3& eye, const core::Vector3& target) : eye_(eye), target_(target) { Update(); }
  void SetPosition(const core::Vector3& position) override { eye_ = position, Update(); }
  void SetTarget(const core::Vector3& target) override { target_ = target, Update(); }
  void UpdateSize(const core::Vector2& size) override {
    projection_ = glm::perspective(glm::radians(45.0f), size.x / size.y, 0.1f, 100.0f);
  }
  const core::Vector3& GetPosition() const override { return eye_; }
  const glm::mat4& GetViewMatrix() override { return view_; }
  const glm::mat4& GetProjectionMatrix() override { return projection_; }
  void ProcessKeyboard(scene::CameraMovement, float) override {}
  void ProcessMouseMovement(float, float, bool) override {}

 private:
  void Update() {
    view_ = glm::lookAt(glm::vec3(eye_.x, eye_.y, eye_.z), glm::vec3(target_.x, target_.y, target_.z),
                        glm::vec3(0.0f, 1.0f, 0.0f));
  }
  core::Vector3 eye_, target_;
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
                                       std::shared_ptr<rendering::Material> material, const core::Vector3& position) {
  auto node = std::make_shared<scene::Node3D>();
  node->SetModel(MakeModel(std::move(mesh)));
  node->SetMaterial(std::move(material));
  node->SetPosition(position);
  viewport.GetScene()->AddNode(node);
  return node;
}

std::shared_ptr<scene::DirectionalLight> AddSun(scene::Viewport& viewport, float ambient = 0.25f) {
  auto sun = std::make_shared<scene::DirectionalLight>();
  sun->SetDirection({-0.4f, -1.0f, -0.6f});
  sun->SetAmbient(ambient);
  viewport.GetScene()->AddNode(sun);
  return sun;
}

std::shared_ptr<rendering::Material> Opaque() { return rendering::Material::Create(); }

}  // namespace

TEST_CASE("lit cubes") {
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(core::Vector3{0, 3, 7}, core::Vector3{0, 0, 0}));
  AddSun(*viewport);
  auto point = std::make_shared<scene::PointLight>();
  point->SetPosition({2.0f, 1.5f, 2.0f});
  point->SetColor({1.0f, 0.6f, 0.2f});
  point->SetRange(6.0f);
  viewport->GetScene()->AddNode(point);
  auto material = Opaque();
  material->shininess = 32.0f;
  AddMesh(*viewport, utils::createCubeWithNormals(1.2f, Color{0.9f, 0.3f, 0.3f, 1.0f}), material, {-1.8f, 0, 0})
      ->SetRotation({0, 30, 0});
  AddMesh(*viewport, utils::createBeveledCubeWithNormals(), material, {0, 0, 0})->SetRotation({20, 45, 0});
  AddMesh(*viewport, utils::createSphereWithNormals(0.7f, 16, 32), material, {1.8f, 0, 0});
  CheckGolden("lit_cubes", viewport);
}

TEST_CASE("shadows") {
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(core::Vector3{0, 5, 6}, core::Vector3{0, 0, 0}));
  AddSun(*viewport, 0.3f)->SetCastsShadows(true);
  auto ground = AddMesh(*viewport, utils::createPlaneWithNormals(8.0f, 8.0f, Color{0.7f, 0.7f, 0.75f, 1.0f}),
                        Opaque(), {0, 0, 0});
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
  viewport->SetCamera(std::make_shared<FixedCamera>(core::Vector3{0, 2, 6}, core::Vector3{0, 0, 0}));
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
  viewport->SetCamera(std::make_shared<FixedCamera>(core::Vector3{0, 7, 7}, core::Vector3{0, 0, 0}));
  AddSun(*viewport);
  auto node = AddMesh(*viewport, utils::createCubeWithNormals(0.7f), Opaque(), {0, 0, 0});
  std::vector<rendering::InstanceData> copies;
  for (int z = 0; z < 5; ++z) {
    for (int x = 0; x < 5; ++x) {
      rendering::InstanceData copy;
      copy.transform = glm::translate(glm::mat4(1.0f), glm::vec3(x - 2.0f, 0.15f * (x + z), z - 2.0f));
      copy.transform = glm::rotate(copy.transform, glm::radians(9.0f * (x * 5 + z)), glm::vec3(0, 1, 0));
      copy.tint = {0.2f + 0.2f * x, 0.9f - 0.15f * z, 0.5f, 1.0f};
      copies.push_back(copy);
    }
  }
  node->SetInstances(std::move(copies));
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
  viewport->GetScene()->AddNode(dots);
  constexpr int kDots = 120;
  std::span<rendering::SpriteInstance> out = dots->Write(kDots);
  for (int i = 0; i < kDots; ++i) {
    const float t = static_cast<float>(i) / kDots;
    const float angle = t * 6.2831853f * 3.0f;
    const float radius = 0.5f + 3.8f * t;
    out[i].position = {radius * std::cos(angle), radius * std::sin(angle)};
    out[i].size = 0.4f + 1.2f * t;
    out[i].color = rendering::PackColor({1.0f - t, 0.4f + 0.4f * t, t, 0.8f});
  }
  dots->FitBounds();
  CheckGolden("sprites", viewport);
}

TEST_CASE("post chain bloom") {
  // Bright shapes glowing: bright pass, blur at a quarter size, combined onto the picture.
  auto viewport = MakeViewport();
  viewport->SetCamera(std::make_shared<FixedCamera>(core::Vector3{0, 2, 6}, core::Vector3{0, 0, 0}));
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
  canvas.Image({{150, 180}, {250, 256}}, glow, {.tint = {1.0f, 0.5f, 0.1f, 1.0f}, .blend = rendering::BlendMode::Additive});
  const auto font = ui::Font::Load(WLW_TEST_FONT);
  REQUIRE(font != nullptr);
  canvas.Text(*font, "Golden 0.3", {128, 52}, 34, ui::Align::Center,
              {.color = {1, 1, 1, 1}, .outline = 0.06f, .shadow_offset = {2, 2}});
  CheckGolden("canvas", viewport);
}

TEST_SUITE_END();
