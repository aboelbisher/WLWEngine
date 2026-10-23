// Tests for the renderer's GL-free scene pass (rendering/scene_gather.h): what GatherScene collects
// from a scene, and what CullScene keeps, with its matrices and depths.
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/rendering/scene_gather.h"
#include "wlw/scene/camera_3d.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/light.h"
#include "wlw/scene/scene.h"
#include "wlw/scene/sprite_batch.h"
#include "wlw/utils/primitives.h"

TEST_SUITE_BEGIN("scene_gather");

using namespace wlw;
using rendering::CullScene;
using rendering::GatherScene;
using rendering::SceneGather;
using rendering::VisibleSet;

namespace {

std::shared_ptr<core::Model<core::Vertex3D>> CubeModel() {
  auto model = std::make_shared<core::Model<core::Vertex3D>>();
  model->meshes.push_back(utils::createCubeWithNormals(1.0f));
  return model;
}

template <typename T = scene::MeshInstance3D>
std::shared_ptr<T> Cube(const Vec3& position) {
  auto node = std::make_shared<T>();
  node->SetModel(CubeModel());
  node->SetPosition(position);
  return node;
}

// A camera at (0, 0, 10) looking down -z: depth = 10 - z.
const glm::mat4 kView = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0), glm::vec3(0, 1, 0));
const glm::mat4 kViewProjection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) * kView;

}  // namespace

TEST_CASE("gather collects drawables lights and sprites") {
  scene::Scene scene;
  auto cube = Cube({0, 0, 0});
  scene.Root().AddChild(cube);
  scene.Root().AddChild(std::make_shared<scene::Node3D>());  // nothing to draw
  auto hidden = Cube({1, 0, 0});
  hidden->SetVisible(false);
  hidden->AddChild(Cube({2, 0, 0}));  // hidden with its parent
  scene.Root().AddChild(hidden);
  auto empty_instanced = Cube<scene::MultiMeshInstance3D>({3, 0, 0});  // no copies: nothing to draw
  scene.Root().AddChild(empty_instanced);
  auto instanced = Cube<scene::MultiMeshInstance3D>({4, 0, 0});
  instanced->WriteInstances(2);
  scene.Root().AddChild(instanced);
  scene.Root().AddChild(std::make_shared<scene::MeshInstance3D>());  // no model
  scene.Root().AddChild(std::make_shared<scene::DirectionalLight3D>());
  scene.Root().AddChild(std::make_shared<scene::Camera3D>());  // draws nothing
  auto dots = std::make_shared<scene::SpriteBatch>(4);
  dots->Write(1)[0] = {};
  scene.Root().AddChild(dots);
  scene.Root().AddChild(std::make_shared<scene::SpriteBatch>(4));  // no dots

  SceneGather gather;
  GatherScene(scene, gather);
  REQUIRE(gather.drawables.size() == 2);
  CHECK(gather.drawables[0].node == cube.get());
  CHECK(gather.drawables[1].node == instanced.get());
  CHECK(gather.lights.size() == 1);
  CHECK(gather.sprites.size() == 1);
  CHECK((!gather.has_casters && !gather.has_receivers));

  gather.Clear();
  CHECK((gather.drawables.empty() && gather.lights.empty() && gather.sprites.empty()));
}

TEST_CASE("gather grows shadow boxes") {
  scene::Scene scene;
  auto a = Cube({-2, 0, 0});
  a->SetCastsShadow(true);
  auto b = Cube({3, 1, 0});
  b->SetCastsShadow(true);
  b->SetReceivesShadow(true);
  scene.Root().AddChild(a);
  scene.Root().AddChild(b);
  SceneGather gather;
  GatherScene(scene, gather);
  REQUIRE((gather.has_casters && gather.has_receivers));
  CHECK(Near(gather.casters.min.x, -2.5f));
  CHECK(Near(gather.casters.max.x, 3.5f));
  CHECK(Near(gather.casters.max.y, 1.5f));
  CHECK(Near(gather.receivers.min.x, 2.5f));
}

TEST_CASE("cull keeps what the camera sees") {
  scene::Scene scene;
  auto front = Cube({0, 0, 0});
  auto behind = Cube({0, 0, 20});  // behind the camera
  auto aside = Cube({50, 0, 0});   // far outside the side planes
  auto near = Cube({0, 0, 5});
  scene.Root().AddChild(front);
  scene.Root().AddChild(behind);
  scene.Root().AddChild(aside);
  scene.Root().AddChild(near);
  SceneGather gather;
  GatherScene(scene, gather);

  VisibleSet visible;
  CullScene(gather, kView, kViewProjection, visible);
  CHECK(visible.culled == 2);
  REQUIRE(visible.nodes.size() == 2);
  for (const VisibleSet::Node& node : visible.nodes) {
    if (node.node == front.get()) {
      CHECK(Near(node.depth, 10.0f));
    } else {
      CHECK(node.node == near.get());
      CHECK(Near(node.depth, 5.0f));
      CHECK(Near(node.model[3].z, 5.0f));
    }
  }

  // Cleared on every call.
  CullScene(gather, kView, kViewProjection, visible);
  CHECK((visible.nodes.size() == 2 && visible.culled == 2));
}

TEST_CASE("cull normal matrices") {
  scene::Scene scene;
  auto scaled = Cube({0, 0, 0});
  scaled->SetScale({2.0f, 1.0f, 1.0f});
  auto instanced = Cube<scene::MultiMeshInstance3D>({1, 0, 0});
  instanced->SetScale({2.0f, 1.0f, 1.0f});
  instanced->WriteInstances(1);
  scene.Root().AddChild(scaled);
  scene.Root().AddChild(instanced);
  SceneGather gather;
  GatherScene(scene, gather);
  VisibleSet visible;
  CullScene(gather, kView, kViewProjection, visible);
  REQUIRE(visible.nodes.size() == 2);
  // core::NormalMatrix: det (2) x the inverse-transpose diag(0.5, 1, 1), the same directions. Instanced
  // nodes get their own part too (each copy's part is added per vertex in the shader).
  const glm::mat3 expected(glm::vec3(1, 0, 0), glm::vec3(0, 2, 0), glm::vec3(0, 0, 2));
  for (const VisibleSet::Node& node : visible.nodes) {
    CHECK(node.normal_matrix == expected);
  }
}

TEST_CASE("cull sprite batches") {
  scene::Scene scene;
  auto inside = std::make_shared<scene::SpriteBatch>(4);
  inside->Write(1)[0] = {};
  inside->SetBounds({-1, -1}, {1, 1});
  auto outside = std::make_shared<scene::SpriteBatch>(4);
  outside->Write(1)[0] = {};
  outside->SetBounds({-1, -1}, {1, 1});
  outside->SetPosition({0, 0, 30});
  scene.Root().AddChild(inside);
  scene.Root().AddChild(outside);
  SceneGather gather;
  GatherScene(scene, gather);
  VisibleSet visible;
  CullScene(gather, kView, kViewProjection, visible);
  CHECK(visible.culled == 1);
  REQUIRE(visible.sprites.size() == 1);
  CHECK(visible.sprites[0].batch == inside.get());
  CHECK(Near(visible.sprites[0].depth, 10.0f));
}
