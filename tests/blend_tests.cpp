// Tests for blend modes (rendering/blend_mode.h): materials' depth-write rule, the render queues and
// back-to-front sorting (render_queue.h), the Canvas's blend state and Image batching, and the
// radial falloff texture (utils/textures.h). CPU only.
#include <memory>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "check.h"
#include "wlw/rendering/material.h"
#include "wlw/rendering/render_queue.h"
#include "wlw/ui/canvas.h"
#include "wlw/utils/textures.h"

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

// A texture without a GPU (Canvas only compares pointers).
class FakeTexture : public rendering::WTexture2D {
 public:
  void Bind(unsigned int) const override {}
  void Unbind() const override {}
  void Update(const void*) override {}
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
  canvas.FillRect({{50, 50}, {60, 60}}, {1, 0.5f, 0, 1});
  canvas.FillRect({{60, 50}, {70, 60}}, {1, 0.5f, 0, 1});  // still additive: same batch
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

static void TestImageBatching() {
  auto dot = std::make_shared<FakeTexture>();
  auto other = std::make_shared<FakeTexture>();
  ui::Canvas canvas;
  canvas.BeginFrame({1000.0f, 1000.0f}, {}, {1000.0f, 1000.0f});

  // Thousands of sprites with one texture: one draw call, and rectangles join it.
  canvas.FillRect({{0, 0}, {10, 10}}, {1, 1, 1, 1});
  for (int i = 0; i < 1000; ++i) canvas.Image({{float(i), 0}, {float(i) + 4, 4}}, dot, {1, 0.5f, 0, 0.8f});
  canvas.FillRect({{0, 20}, {10, 30}}, {1, 1, 1, 1});
  CHECK(canvas.Batches().size() == 1);
  CHECK(canvas.Batches()[0].texture == dot && canvas.Batches()[0].quad_count == 1002);

  // Another texture, or another blend mode: a new run.
  canvas.Image({{0, 0}, {4, 4}}, other);
  canvas.SetBlendMode(BlendMode::Additive);
  canvas.Image({{0, 0}, {4, 4}}, other);
  canvas.Image({{8, 0}, {12, 4}}, other);
  CHECK(canvas.Batches().size() == 3);
  CHECK(canvas.Batches()[1].texture == other && canvas.Batches()[1].quad_count == 1);
  CHECK(canvas.Batches()[2].blend == BlendMode::Additive && canvas.Batches()[2].quad_count == 2);

  // Text samples the font atlas, an image its texture: they never share a run.
  canvas.SetBlendMode(BlendMode::Alpha);
  canvas.Image({{0, 0}, {4, 4}}, dot);
  CHECK(canvas.Batches().size() == 4 && canvas.Batches()[3].font == nullptr);

  // Vertices: the rect's corners, the uv sub-rectangle, the tint, kind 4 (image).
  canvas.BeginFrame({1000.0f, 1000.0f}, {}, {1000.0f, 1000.0f});
  canvas.Image({{10, 20}, {30, 60}}, dot, {0.2f, 0.4f, 1.0f, 0.5f}, {{0.5f, 0.0f}, {1.0f, 0.5f}});
  const auto& v = canvas.Vertices();
  CHECK(canvas.QuadCount() == 1);
  CHECK(v[0].position == glm::vec2(10, 20) && v[2].position == glm::vec2(30, 60));
  CHECK(v[0].uv == glm::vec2(0.5f, 0.0f) && v[2].uv == glm::vec2(1.0f, 0.5f));
  CHECK(v[0].params.x == 4.0f && v[0].color.a == 0.5f);

  canvas.Image({{0, 0}, {4, 4}}, nullptr);  // no texture: nothing drawn
  CHECK(canvas.QuadCount() == 1);
}

static void TestRadialFalloff() {
  const int size = 64;
  const std::vector<uint8_t> pixels = utils::RadialFalloffPixels(size);
  CHECK(pixels.size() == size * size * 4);
  auto alpha = [&](int x, int y) { return static_cast<int>(pixels[(y * size + x) * 4 + 3]); };
  CHECK(pixels[0] == 255 && pixels[1] == 255 && pixels[2] == 255);  // white: the tint gives the colour
  CHECK(alpha(32, 32) > 250);                                          // full at the centre
  CHECK(alpha(0, 0) == 0 && alpha(63, 0) == 0 && alpha(0, 63) == 0);   // nothing in the corners
  CHECK(alpha(0, 32) < 10);                                            // fades out by the edge
  // Symmetric, and falling steadily from the centre outwards.
  CHECK(alpha(10, 32) == alpha(53, 32) && alpha(32, 10) == alpha(32, 53) && alpha(10, 32) == alpha(32, 10));
  bool falling = true;
  for (int x = 33; x < size; ++x) falling = falling && alpha(x, 32) <= alpha(x - 1, 32);
  CHECK(falling);
  CHECK(utils::RadialFalloffPixels(0).size() == 4);  // at least one texel
}

int main() {
  TestDepthWriteRule();
  TestQueues();
  TestBackToFront();
  TestCanvasBlendBatches();
  TestImageBatching();
  TestRadialFalloff();
  return TestResult("blend");
}
