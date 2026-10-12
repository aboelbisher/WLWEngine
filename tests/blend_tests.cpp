// Tests for blend modes (rendering/blend_mode.h): materials' depth-write rule, the render queues and
// back-to-front sorting (render_queue.h), and the Canvas's blend state and SoftCircle. CPU only.
#include <memory>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "check.h"
#include "wlw/rendering/material.h"
#include "wlw/rendering/render_queue.h"
#include "wlw/ui/canvas.h"

using namespace wlw;
using rendering::BlendMode;
using rendering::RenderQueue;

namespace {

// Material is abstract (its texture binding is GL): this one binds nothing.
class TestMaterial : public rendering::Material {
 public:
  void BindTexture() const override {}
  void UnbindTexture() const override {}
};

struct Item {
  int id;
  glm::vec3 center;
};

std::vector<int> Ids(const std::vector<Item>& items) {
  std::vector<int> ids;
  for (const Item& item : items) ids.push_back(item.id);
  return ids;
}

}  // namespace

static void TestDepthWriteRule() {
  TestMaterial material;
  CHECK(material.blend == BlendMode::Opaque && material.DepthWrite());  // solid: writes depth
  for (BlendMode blend : {BlendMode::Alpha, BlendMode::Additive, BlendMode::Multiply}) {
    material.blend = blend;
    CHECK(!material.DepthWrite());  // see-through: must not hide what's drawn after it
  }
  material.depth_write = true;  // explicit override
  CHECK(material.DepthWrite());
  material.blend = BlendMode::Opaque;
  material.depth_write = false;
  CHECK(!material.DepthWrite());
}

static void TestQueues() {
  CHECK(rendering::QueueFor(BlendMode::Opaque) == RenderQueue::Opaque);
  CHECK(rendering::QueueFor(BlendMode::Alpha) == RenderQueue::Transparent);
  CHECK(rendering::QueueFor(BlendMode::Additive) == RenderQueue::Transparent);
  CHECK(rendering::QueueFor(BlendMode::Multiply) == RenderQueue::Transparent);
}

static void TestBackToFront() {
  auto center = [](const Item& item) { return item.center; };

  // A perspective-style camera at z = 10 looking down -z: smaller z = farther.
  const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  std::vector<Item> items = {{1, {0.0f, 0.0f, 5.0f}}, {2, {0.0f, 0.0f, -20.0f}}, {3, {3.0f, 1.0f, 0.0f}}};
  rendering::SortBackToFront(items, view, center);
  CHECK((Ids(items) == std::vector<int>{2, 3, 1}));

  // Distance is measured along the view direction, not from the camera point: sideways offsets
  // don't change the order (what an orthographic camera needs).
  std::vector<Item> sideways = {{1, {50.0f, 0.0f, 1.0f}}, {2, {0.0f, 0.0f, 0.0f}}};
  rendering::SortBackToFront(sideways, view, center);
  CHECK((Ids(sideways) == std::vector<int>{2, 1}));

  // A tilted top-down camera (like Bluzzle's): looking down and forward along -z.
  const glm::mat4 tilted = glm::lookAt(glm::vec3(0.0f, 20.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  std::vector<Item> board = {{1, {0.0f, 0.0f, 3.0f}}, {2, {0.0f, 0.0f, -3.0f}}, {3, {0.0f, 2.0f, 0.0f}}};
  rendering::SortBackToFront(board, tilted, center);
  CHECK((Ids(board) == std::vector<int>{2, 1, 3}));  // the far row, the near row, then what's raised towards the camera

  // Equal distances keep their order (stable: no flicker between frames).
  std::vector<Item> ties = {{1, {0.0f, 0.0f, 0.0f}}, {2, {1.0f, 0.0f, 0.0f}}, {3, {-1.0f, 0.0f, 0.0f}}};
  rendering::SortBackToFront(ties, view, center);
  CHECK((Ids(ties) == std::vector<int>{1, 2, 3}));
}

static void TestCanvasBlendBatches() {
  ui::Canvas canvas;
  canvas.BeginFrame({1000.0f, 1000.0f}, {}, {1000.0f, 1000.0f});
  CHECK(canvas.GetBlendMode() == BlendMode::Alpha);

  canvas.FillRect({{0, 0}, {10, 10}}, {1, 1, 1, 1});
  canvas.FillRect({{20, 0}, {30, 10}}, {1, 1, 1, 1});  // same mode: same batch
  canvas.SetBlendMode(BlendMode::Additive);
  canvas.SoftCircle({50, 50}, 10.0f, {1, 0.5f, 0, 1});
  canvas.SoftCircle({60, 50}, 10.0f, {1, 0.5f, 0, 1});  // still additive: same batch
  canvas.SetBlendMode(BlendMode::Alpha);
  canvas.FillRect({{0, 80}, {10, 90}}, {1, 1, 1, 1});

  const auto& batches = canvas.Batches();
  CHECK(batches.size() == 3);
  CHECK(batches[0].blend == BlendMode::Alpha && batches[0].quad_count == 2);
  CHECK(batches[1].blend == BlendMode::Additive && batches[1].quad_count == 2);
  CHECK(batches[2].blend == BlendMode::Alpha && batches[2].quad_count == 1);

  // The mode doesn't leak into the next frame.
  canvas.SetBlendMode(BlendMode::Multiply);
  canvas.BeginFrame({1000.0f, 1000.0f}, {}, {1000.0f, 1000.0f});
  CHECK(canvas.GetBlendMode() == BlendMode::Alpha && canvas.Batches().empty());

  // A ShaderRect takes the current mode too.
  auto material = rendering::ShaderMaterial::Create(nullptr);
  canvas.SetBlendMode(BlendMode::Additive);
  canvas.ShaderRect({{0, 0}, {100, 100}}, material);
  CHECK(canvas.Batches().size() == 1 && canvas.Batches()[0].blend == BlendMode::Additive);
}

static void TestSoftCircle() {
  ui::Canvas canvas;
  canvas.BeginFrame({1000.0f, 1000.0f}, {}, {1000.0f, 1000.0f});
  canvas.SoftCircle({100.0f, 200.0f}, 25.0f, {0.2f, 0.4f, 1.0f, 0.5f});
  CHECK(canvas.QuadCount() == 1);
  const auto& v = canvas.Vertices();
  // A square around the centre, uv -1..1 (the shader fades by distance), kind 4.
  CHECK(v[0].position == glm::vec2(75.0f, 175.0f) && v[2].position == glm::vec2(125.0f, 225.0f));
  CHECK(v[0].uv == glm::vec2(-1.0f, -1.0f) && v[2].uv == glm::vec2(1.0f, 1.0f));
  CHECK(v[0].params.x == 4.0f && v[0].color.a == 0.5f);

  canvas.SoftCircle({0.0f, 0.0f}, 0.0f, {1, 1, 1, 1});  // nothing to draw
  CHECK(canvas.QuadCount() == 1);
}

int main() {
  TestDepthWriteRule();
  TestQueues();
  TestBackToFront();
  TestCanvasBlendBatches();
  TestSoftCircle();
  return TestResult("blend");
}
