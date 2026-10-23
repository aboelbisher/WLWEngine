// The renderer's CPU side, without a GPU: the scene walk (gather), frustum culling with matrices
// and depths (cull), the draw-order sort, and node transforms. At 1k and 10k nodes. Plus the bounds
// of an instanced node's 5k copies (recomputed after each write).
#include <memory>

#include <glm/gtc/matrix_transform.hpp>

#include "bench.h"
#include "wlw/core/random.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/rendering/render_list.h"
#include "wlw/rendering/scene_gather.h"
#include "wlw/scene/geometry_instance.h"
#include "wlw/scene/light.h"
#include "wlw/scene/node.h"
#include "wlw/scene/scene.h"
#include "wlw/utils/primitives.h"

using namespace wlw;

namespace {

// `count` cubes on a square grid on the ground (y = 0), one in four casting shadows, plus a sun.
// The camera (CameraMatrices) sees about a tenth of them (917 of 10k, 107 of 1k): culling has work both ways.
std::shared_ptr<scene::Scene> GridScene(int count) {
  auto scene = scene::Scene::Create();
  auto model = std::make_shared<core::Model<core::Vertex3D>>();
  model->meshes.push_back(utils::createCubeWithNormals(0.8f));
  const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
  for (int i = 0; i < count; ++i) {
    auto node = std::make_shared<scene::MeshInstance3D>();
    node->SetModel(model);
    node->SetPosition({static_cast<float>(i % side - side / 2), 0.0f, static_cast<float>(i / side - side / 2)});
    node->SetCastsShadow(i % 4 == 0);
    node->SetReceivesShadow(true);
    scene->Root().AddChild(node);
  }
  auto sun = std::make_shared<scene::DirectionalLight3D>();
  sun->SetCastsShadows(true);
  scene->Root().AddChild(sun);
  return scene;
}

struct CameraMatrices {
  glm::mat4 view, view_projection;
};

// Above one edge of the grid, looking across it: the near half is in view.
CameraMatrices Camera(int count) {
  const float half = std::sqrt(static_cast<float>(count)) * 0.5f;
  CameraMatrices camera;
  camera.view = glm::lookAt(glm::vec3(0.0f, half * 0.4f, half), glm::vec3(0.0f), glm::vec3(0, 1, 0));
  camera.view_projection = glm::perspective(glm::radians(50.0f), 9.0f / 16.0f, 0.1f, half * 1.2f) * camera.view;
  return camera;
}

void RenderBenchmarks(bench::Context& ctx, int count, const char* size) {
  const std::string suffix = std::string(" ") + size;
  auto scene = GridScene(count);
  const CameraMatrices camera = Camera(count);
  rendering::SceneGather gather;
  ctx.Run("render/gather" + suffix, [&] {
    gather.Clear();
    rendering::GatherScene(*scene, gather);
    bench::Context::Keep(gather.drawables.size());
  });

  gather.Clear();
  rendering::GatherScene(*scene, gather);
  rendering::VisibleSet visible;
  ctx.Run("render/cull" + suffix, [&] {
    rendering::CullScene(gather, camera.view, camera.view_projection, visible);
    bench::Context::Keep(visible.nodes.size());
  });

  // Keys as the driver makes them: a few programs and textures, depth from the cull.
  rendering::CullScene(gather, camera.view, camera.view_projection, visible);
  std::vector<rendering::DrawOrder> unsorted;
  for (uint32_t i = 0; i < visible.nodes.size(); ++i) {
    const float depth = visible.nodes[i].depth;
    const uint64_t key =
        i % 5 == 0 ? rendering::TransparentSortKey(depth, i % 3) : rendering::OpaqueSortKey(i % 6, i % 4, depth);
    unsorted.push_back({key, i});
  }
  std::vector<rendering::DrawOrder> order;
  order.reserve(unsorted.size());
  ctx.Run("render/sort" + suffix, [&] {
    order.assign(unsorted.begin(), unsorted.end());
    rendering::SortDrawOrder(order);
    bench::Context::Keep(order.front().key);
  });
}

// Moving nodes and reading their world matrices: flat (top-level nodes) and nested (chains of 4:
// turning a root dirties its chain). "read nested" reads the same matrices with nothing moved.
void TransformBenchmarks(bench::Context& ctx, int count, const char* size) {
  const std::string suffix = std::string(" ") + size;
  std::vector<std::shared_ptr<scene::Node3D>> flat;
  for (int i = 0; i < count; ++i) flat.push_back(std::make_shared<scene::Node3D>());
  float t = 0.0f;
  ctx.Run("transform/move flat" + suffix, [&] {
    t += 0.01f;
    float sum = 0.0f;
    for (size_t i = 0; i < flat.size(); ++i) {
      flat[i]->SetPosition({static_cast<float>(i), t, 0.0f});
      flat[i]->SetRotation(glm::angleAxis(t * 10.0f, Vec3(0.0f, 1.0f, 0.0f)));
      sum += flat[i]->GetWorldMatrix()[3].y;
    }
    bench::Context::Keep(sum);
  });

  std::vector<std::shared_ptr<scene::Node3D>> roots, all;
  for (int i = 0; i < count; i += 4) {
    auto parent = std::make_shared<scene::Node3D>();
    roots.push_back(parent);
    all.push_back(parent);
    for (int depth = 1; depth < 4; ++depth) {
      auto child = std::make_shared<scene::Node3D>();
      child->SetPosition({0.0f, 1.0f, 0.0f});
      parent->AddChild(child);
      all.push_back(child);
      parent = child;
    }
  }
  ctx.Run("transform/move nested" + suffix, [&] {
    t += 0.01f;
    for (auto& root : roots) root->SetRotation(glm::angleAxis(t * 10.0f, Vec3(0.0f, 1.0f, 0.0f)));
    float sum = 0.0f;
    for (auto& node : all) sum += node->GetWorldMatrix()[3].x;
    bench::Context::Keep(sum);
  });
  // Nothing moves: what the world-matrix cache saves on a static scene.
  ctx.Run("transform/read nested" + suffix, [&] {
    float sum = 0.0f;
    for (auto& node : all) sum += node->GetWorldMatrix()[3].x;
    bench::Context::Keep(sum);
  });
}

void InstanceBoundsBenchmark(bench::Context& ctx) {
  // 5k copies with random rotations, scales and positions (a particle field): one op = one write +
  // the bounds the renderer asks for after it.
  constexpr size_t kCopies = 5000;
  scene::MultiMeshInstance3D node;
  auto model = std::make_shared<core::Model<core::Vertex3D>>();
  model->meshes.push_back(utils::createCubeWithNormals(1.0f));
  node.SetModel(model);
  core::Random rng(42);
  for (rendering::Instance& copy : node.WriteInstances(kCopies)) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(rng.Range(-20, 20), rng.Range(0, 10), rng.Range(-20, 20)));
    m = glm::rotate(m, rng.Range(0, 6.28f), glm::normalize(glm::vec3(rng.Range(-1, 1), 1.0f, rng.Range(-1, 1))));
    copy.SetTransform(glm::scale(m, glm::vec3(rng.Range(0.1f, 0.5f))));
  }
  ctx.Run("render/instance bounds 5k", [&] {
    node.WriteInstances(kCopies);  // same copies, new version: the cache is stale
    bench::Context::Keep(node.InstanceBounds().max.x);
  });
}

}  // namespace

WLW_BENCHMARK("render") {
  RenderBenchmarks(ctx, 1000, "1k");
  RenderBenchmarks(ctx, 10000, "10k");
  InstanceBoundsBenchmark(ctx);
}

WLW_BENCHMARK("transform") {
  TransformBenchmarks(ctx, 1000, "1k");
  TransformBenchmarks(ctx, 10000, "10k");
}
