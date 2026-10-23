// Tests for blend modes (rendering/blend_mode.h): materials' depth-write rule, per-draw blending on
// the Canvas, ShaderMaterial defaults, Image batching, and the radial falloff texture
// (utils/textures.h). CPU only. (Draw order: render_list_tests.)
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "test_helpers.h"
#include "wlw/rendering/material.h"
#include "wlw/rendering/shader.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"
#include "wlw/ui/canvas.h"
#include "wlw/utils/textures.h"

TEST_SUITE_BEGIN("blend");

using namespace wlw;
using rendering::BlendMode;

namespace {

// Textures and shaders need no GPU to be made (the renderer uploads / compiles them later).
std::shared_ptr<rendering::WTexture2D> Texture() { return rendering::WTexture2D::Create(4, 4); }
std::shared_ptr<rendering::Shader> ShaderOf(const char* type) {
  return rendering::Shader::Create(std::string("shader_type ") + type + "; void fragment() { FragColor = vec4(1.0); }");
}

}  // namespace

TEST_CASE("depth write rule") {
  rendering::Material material;
  CHECK((material.blend == BlendMode::Opaque && material.DepthWrite()));  // solid: writes depth
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

TEST_CASE("canvas blend per draw") {
  auto dot = Texture();
  ui::Canvas canvas;
  canvas.Internal().BeginFrame({1000.0f, 1000.0f}, {1000.0f, 1000.0f});

  // UI shapes are Alpha; an image says its own mode; no state carries over between calls.
  canvas.FillRect({{0, 0}, {10, 10}}, {1, 1, 1, 1});
  canvas.Image({{0, 0}, {4, 4}}, dot, {.blend = BlendMode::Additive});
  canvas.Image({{4, 0}, {8, 4}}, dot, {.blend = BlendMode::Additive});  // same texture + mode: same run
  canvas.FillRect({{0, 80}, {10, 90}}, {1, 1, 1, 1});                   // back to Alpha by itself
  const auto& batches = canvas.Batches();
  CHECK(batches.size() == 3);
  CHECK((batches[0].blend == BlendMode::Alpha && batches[0].quad_count == 1));
  CHECK((batches[1].blend == BlendMode::Additive && batches[1].quad_count == 2));
  CHECK((batches[2].blend == BlendMode::Alpha && batches[2].quad_count == 1));
}

TEST_CASE("shader material blend") {
  // Like Godot: canvas shaders start Alpha (they draw over the UI), mesh shaders Opaque.
  std::shared_ptr<rendering::ShaderMaterial> canvas_material = rendering::ShaderMaterial::Create(ShaderOf("canvas"));
  std::shared_ptr<rendering::ShaderMaterial> mesh_material = rendering::ShaderMaterial::Create(ShaderOf("mesh"));
  CHECK(canvas_material->blend == BlendMode::Alpha);
  CHECK((mesh_material->blend == BlendMode::Opaque && mesh_material->DepthWrite()));

  // A ShaderRect blends as its material says.
  ui::Canvas canvas;
  canvas.Internal().BeginFrame({1000.0f, 1000.0f}, {1000.0f, 1000.0f});
  canvas.ShaderRect({{0, 0}, {100, 100}}, canvas_material);
  canvas_material->blend = BlendMode::Additive;
  canvas.ShaderRect({{0, 0}, {100, 100}}, canvas_material);
  CHECK(canvas.Batches().size() == 2);
  CHECK((canvas.Batches()[0].blend == BlendMode::Alpha && canvas.Batches()[1].blend == BlendMode::Additive));
}

TEST_CASE("image batching") {
  auto dot = Texture();
  auto other = Texture();
  ui::Canvas canvas;
  canvas.Internal().BeginFrame({1000.0f, 1000.0f}, {1000.0f, 1000.0f});

  // Thousands of sprites with one texture: one draw call, and rectangles join it.
  canvas.FillRect({{0, 0}, {10, 10}}, {1, 1, 1, 1});
  for (int i = 0; i < 1000; ++i) canvas.Image({{float(i), 0}, {float(i) + 4, 4}}, dot, {.tint = {1, 0.5f, 0, 0.8f}});
  canvas.FillRect({{0, 20}, {10, 30}}, {1, 1, 1, 1});
  CHECK(canvas.Batches().size() == 1);
  CHECK((canvas.Batches()[0].texture == dot && canvas.Batches()[0].quad_count == 1002));

  // Another texture, or another blend mode: a new run.
  canvas.Image({{0, 0}, {4, 4}}, other);
  canvas.Image({{0, 0}, {4, 4}}, other, {.blend = BlendMode::Additive});
  canvas.Image({{8, 0}, {12, 4}}, other, {.blend = BlendMode::Additive});
  CHECK(canvas.Batches().size() == 3);
  CHECK((canvas.Batches()[1].texture == other && canvas.Batches()[1].quad_count == 1));
  CHECK((canvas.Batches()[2].blend == BlendMode::Additive && canvas.Batches()[2].quad_count == 2));

  canvas.Image({{0, 0}, {4, 4}}, dot);
  CHECK((canvas.Batches().size() == 4 && canvas.Batches()[3].font == nullptr));

  // Vertices: the rect's corners, the uv sub-rectangle, the tint, kind 4 (image).
  canvas.Internal().BeginFrame({1000.0f, 1000.0f}, {1000.0f, 1000.0f});
  canvas.Image({{10, 20}, {30, 60}}, dot, {.tint = {0.2f, 0.4f, 1.0f, 0.5f}, .uv = {{0.5f, 0.0f}, {1.0f, 0.5f}}});
  const auto& v = canvas.Vertices();
  CHECK(canvas.QuadCount() == 1);
  CHECK((v[0].position == glm::vec2(10, 20) && v[2].position == glm::vec2(30, 60)));
  CHECK((v[0].uv == glm::vec2(0.5f, 0.0f) && v[2].uv == glm::vec2(1.0f, 0.5f)));
  CHECK((v[0].params.x == 4.0f && v[0].color.a == 0.5f));

  canvas.Image({{0, 0}, {4, 4}}, nullptr);  // no texture: nothing drawn
  CHECK(canvas.QuadCount() == 1);
}

TEST_CASE("radial falloff") {
  const int size = 64;
  const std::vector<uint8_t> pixels = utils::RadialFalloffPixels(size);
  CHECK(pixels.size() == size * size * 4);
  auto alpha = [&](int x, int y) { return static_cast<int>(pixels[(y * size + x) * 4 + 3]); };
  CHECK((pixels[0] == 255 && pixels[1] == 255 && pixels[2] == 255));    // white: the tint gives the colour
  CHECK(alpha(32, 32) > 250);                                           // full at the centre
  CHECK((alpha(0, 0) == 0 && alpha(63, 0) == 0 && alpha(0, 63) == 0));  // nothing in the corners
  CHECK(alpha(0, 32) < 10);                                             // fades out by the edge
  // Symmetric, and falling steadily from the centre outwards.
  CHECK((alpha(10, 32) == alpha(53, 32) && alpha(32, 10) == alpha(32, 53) && alpha(10, 32) == alpha(32, 10)));
  bool falling = true;
  for (int x = 33; x < size; ++x) falling = falling && alpha(x, 32) <= alpha(x - 1, 32);
  CHECK(falling);
  CHECK(utils::RadialFalloffPixels(0).size() == 4);  // at least one texel
}

TEST_SUITE_END();
