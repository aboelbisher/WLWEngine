// Tests for the renderer's GL-free scene pass (rendering/scene_gather.h): what GatherScene collects
// from a scene, and what CullScene keeps, with its matrices and depths.
#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/rendering/scene_gather.h"
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

std::shared_ptr<scene::Node3D> Cube(const core::Vector3& position) {
  auto node = std::make_shared<scene::Node3D>();
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
  scene.AddNode(cube);
  scene.AddNode(std::make_shared<scene::Node3D>());  // nothing to draw
  auto hidden = Cube({1, 0, 0});
  hidden->SetVisible(false);
  hidden->AddNode(Cube({2, 0, 0}));  // hidden with its parent
  scene.AddNode(hidden);
  auto empty_instanced = Cube({3, 0, 0});
  empty_instanced->SetInstances({});
  scene.AddNode(empty_instanced);
  scene.AddNode(std::make_shared<scene::DirectionalLight>());
  auto dots = std::make_shared<scene::SpriteBatch>(4);
  dots->Write(1)[0] = {};
  scene.AddNode(dots);
  scene.AddNode(std::make_shared<scene::SpriteBatch>(4));  // no dots

  SceneGather gather;
  GatherScene(scene, gather);
  CHECK(gather.drawables.size() == 1);
  CHECK(gather.drawables[0].node == cube.get());
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
  scene.AddNode(a);
  scene.AddNode(b);
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
  scene.AddNode(front);
  scene.AddNode(behind);
  scene.AddNode(aside);
  scene.AddNode(near);
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
  auto instanced = Cube({1, 0, 0});
  instanced->SetInstances({rendering::InstanceData{}});
  scene.AddNode(scaled);
  scene.AddNode(instanced);
  SceneGather gather;
  GatherScene(scene, gather);
  VisibleSet visible;
  CullScene(gather, kView, kViewProjection, visible);
  REQUIRE(visible.nodes.size() == 2);
  for (const VisibleSet::Node& node : visible.nodes) {
    if (node.node == scaled.get()) {
      CHECK(Near(node.normal_matrix[0][0], 0.5f));  // inverse-transpose of a scale of 2
      CHECK(Near(node.normal_matrix[1][1], 1.0f));
    } else {
      CHECK(node.normal_matrix == glm::mat3(1.0f));  // instanced: per vertex in the shader
    }
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
  scene.AddNode(inside);
  scene.AddNode(outside);
  SceneGather gather;
  GatherScene(scene, gather);
  VisibleSet visible;
  CullScene(gather, kView, kViewProjection, visible);
  CHECK(visible.culled == 1);
  REQUIRE(visible.sprites.size() == 1);
  CHECK(visible.sprites[0].batch == inside.get());
  CHECK(Near(visible.sprites[0].depth, 10.0f));
}
