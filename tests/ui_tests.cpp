// Tests for the UI layer's CPU side: font baking, text measuring/layout, Canvas units and batching
// (incl. ShaderRect), a ShaderMaterial's params.
// (Drawing itself needs a GPU; these check everything that's computed before it.)
#include <cmath>
#include <memory>
#include <string>
#include <variant>

#include "check.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"
#include "wlw/ui/layout.h"

using namespace wlw::ui;

static std::shared_ptr<Font> LoadTestFont() {
  auto font = Font::Load(WLW_TEST_FONT);  // path set by tests/CMakeLists.txt
  CHECK(font != nullptr);
  return font;
}

static void TestFontBakes(const Font& font) {
  const Font::Atlas& atlas = font.GetAtlas();
  CHECK(atlas.width == 512 && atlas.height > 0);
  CHECK(atlas.pixels.size() == static_cast<size_t>(atlas.width) * atlas.height);
  bool has_inside = false;  // SDF: some texels are inside a glyph (> 0.5 = 128)
  for (uint8_t p : atlas.pixels) has_inside = has_inside || p > 200;
  CHECK(has_inside);
  CHECK(font.Ascent(100.0f) > 0.0f && font.Descent(100.0f) > 0.0f);
  CHECK(Near(font.Ascent(100.0f) + font.Descent(100.0f), 100.0f, 0.5f));  // size = line height
}

static void TestMeasure(const Font& font) {
  CHECK(font.Measure("", 40.0f) == 0.0f);
  const float w20 = font.Measure("Score 123", 20.0f);
  const float w40 = font.Measure("Score 123", 40.0f);
  CHECK(w20 > 0.0f && Near(w40, 2.0f * w20, 1e-3f));  // linear in size
  CHECK(font.Measure("AV", 40.0f) <= font.Measure("A", 40.0f) + font.Measure("V", 40.0f) + 1e-4f);  // kerning
  CHECK(font.Measure("WWW", 40.0f) > font.Measure("iii", 40.0f));
}

static void TestLayout(const Font& font) {
  std::vector<GlyphQuad> quads;
  const glm::vec2 pen(10.0f, 100.0f);
  font.Layout("A g", 40.0f, pen, quads);
  CHECK(quads.size() == 2);  // the space has no quad, only an advance
  // Left to right. (Quads overlap a little: each includes the distance field's padding.)
  CHECK((quads[0].min.x + quads[0].max.x) < (quads[1].min.x + quads[1].max.x));
  CHECK(quads[0].min.y < pen.y);                   // 'A' is above the baseline...
  CHECK(quads[1].max.y > pen.y);                   // ...'g' hangs below it
  for (const GlyphQuad& q : quads) CHECK(q.uv_min.x < q.uv_max.x && q.uv_max.y <= 1.0f);

  quads.clear();
  font.Layout("\xC3\xA9", 40.0f, pen, quads);  // UTF-8 'e acute': 2 non-ASCII bytes -> 2 '?'
  CHECK(quads.size() == 2);
}

static void TestOutlineEdge(const Font& font) {
  CHECK(Near(font.EdgeForOutline(0.0f), 0.5f));
  CHECK(font.EdgeForOutline(0.05f) < 0.5f);
  CHECK(Near(font.EdgeForOutline(10.0f), 0.02f));  // can't go beyond the baked padding
}

static void TestCanvasUnits() {
  Canvas canvas;
  wlw::platform::SafeAreaInsets insets;
  insets.top = 100.0f;    // status bar / camera cutout
  insets.bottom = 54.0f;  // gesture bar
  canvas.BeginFrame({1080.0f, 2400.0f}, insets, {400.0f, 720.0f});
  CHECK(Near(canvas.PixelsPerUnit(), 2.7f));         // the width limits: 1080 / 400
  CHECK(Near(canvas.Size().x, 400.0f) && Near(canvas.Size().y, 2400.0f / 2.7f, 1e-2f));
  CHECK(Near(canvas.SafeArea().min.y, 100.0f / 2.7f, 1e-3f));
  CHECK(Near(canvas.SafeArea().max.y, (2400.0f - 54.0f) / 2.7f, 1e-2f));
  CHECK(Near(canvas.FromPixels({270.0f, 0.0f}).x, 100.0f));

  canvas.BeginFrame({2400.0f, 1080.0f}, {}, {400.0f, 720.0f});  // landscape: the height limits
  CHECK(Near(canvas.PixelsPerUnit(), 1.5f));
}

static void TestCanvasTextAndBatches(const Font& font_a) {
  Canvas canvas;
  canvas.BeginFrame({400.0f, 800.0f}, {}, {400.0f, 800.0f});

  canvas.Text(font_a, "123", {200.0f, 50.0f}, 40.0f, Align::Center);
  CHECK(canvas.QuadCount() == 3);
  float min_x = 1e9f, max_x = -1e9f;
  for (const auto& v : canvas.Vertices()) {
    min_x = std::min(min_x, v.position.x);
    max_x = std::max(max_x, v.position.x);
  }
  CHECK(Near((min_x + max_x) * 0.5f, 200.0f, 3.0f));  // centred on the point

  // Rectangles join the current batch; another font needs its own.
  canvas.FillRect({{0, 0}, {50, 20}}, {1, 0, 0, 1}, 6.0f);
  canvas.Text(font_a, "45", {100.0f, 100.0f}, 20.0f);
  CHECK(canvas.Batches().size() == 1 && canvas.Batches()[0].quad_count == 6);
  auto font_b = Font::Load(WLW_TEST_FONT);  // same file, but a separate atlas
  canvas.Text(*font_b, "6", {100.0f, 150.0f}, 20.0f);
  CHECK(canvas.Batches().size() == 2 && canvas.Batches()[1].first_quad == 6);

  // Outline and shadow are extra copies of the glyphs, drawn first (behind).
  canvas.BeginFrame({400.0f, 800.0f}, {}, {400.0f, 800.0f});
  TextStyle style;
  style.outline = 0.06f;
  style.shadow_offset = {2.0f, 3.0f};
  canvas.Text(font_a, "7", {100.0f, 100.0f}, 40.0f, Align::Left, style);
  CHECK(canvas.QuadCount() == 3);
  CHECK(canvas.Vertices()[0].params.y < 0.5f);   // shadow uses the outline's edge
  CHECK(canvas.Vertices()[8].params.y == 0.5f);  // fill: the normal edge

  // Rounded rect: half size and a radius clamped to fit.
  canvas.FillRect({{0, 0}, {40, 10}}, {1, 1, 1, 1}, 50.0f);
  const auto& rect = canvas.Vertices().back();
  CHECK(rect.params.x == 2.0f && Near(rect.params.y, 20.0f) && Near(rect.params.z, 5.0f) && Near(rect.params.w, 5.0f));
}

static void TestCanvasLine() {
  Canvas canvas;
  canvas.BeginFrame({400.0f, 800.0f}, {}, {400.0f, 800.0f});
  canvas.Line({10.0f, 20.0f}, {30.0f, 20.0f}, 4.0f, {1, 1, 1, 1});  // horizontal, 20 long
  CHECK(canvas.QuadCount() == 1);
  const auto& v = canvas.Vertices();
  // A capsule: half length 10 + 2 (the round ends), half thickness 2, radius 2.
  CHECK(Near(v[0].params.y, 12.0f) && Near(v[0].params.z, 2.0f) && Near(v[0].params.w, 2.0f));
  CHECK(Near(v[0].position.x, 8.0f) && Near(v[0].position.y, 18.0f) && Near(v[2].position.x, 32.0f));

  canvas.Line({0.0f, 0.0f}, {30.0f, 40.0f}, 2.0f, {1, 1, 1, 1});  // diagonal, 50 long
  for (size_t i = 4; i < 8; ++i) {
    // Every corner is the same distance from the centre (15, 20): a rotated rectangle.
    CHECK(Near(glm::length(v[i].position - glm::vec2(15.0f, 20.0f)), std::sqrt(26.0f * 26.0f + 1.0f), 1e-3f));
    CHECK(Near(std::fabs(v[i].uv.x), 26.0f) && Near(std::fabs(v[i].uv.y), 1.0f));  // uv in the line's own frame
  }
  canvas.Line({5.0f, 5.0f}, {5.0f, 5.0f}, 2.0f, {1, 1, 1, 1});  // zero length: nothing
  CHECK(canvas.QuadCount() == 2);
}

static void TestLayoutAndToggle(const Font& font) {
  const Rect area{{0.0f, 0.0f}, {100.0f, 50.0f}};
  CHECK(Near(Centered({10.0f, 20.0f}, {4.0f, 6.0f}).min.x, 8.0f) && Near(Centered({10.0f, 20.0f}, {4.0f, 6.0f}).max.y, 23.0f));
  CHECK(Near(Inset(area, 5.0f).min.x, 5.0f) && Near(Inset(area, 5.0f).max.y, 45.0f));
  const Rect last_row = Row(area, 1, 2, 10.0f);  // two rows of 20 with a 10 gap
  CHECK(Near(last_row.min.y, 30.0f) && Near(last_row.max.y, 50.0f) && Near(last_row.max.x, 100.0f));
  const Rect middle = Column(area, 1, 3, 5.0f);  // three columns of 30 with 5 gaps
  CHECK(Near(middle.min.x, 35.0f) && Near(middle.max.x, 65.0f));

  Canvas canvas;
  canvas.BeginFrame({400.0f, 800.0f}, {}, {400.0f, 800.0f});
  PointerState tap;
  tap.position = {50.0f, 25.0f};
  tap.pressed = tap.released = true;
  canvas.SetPointer(tap);
  bool sound = true;
  CHECK(canvas.Toggle(font, "SOUND", sound, area) && !sound);  // clicked: flipped
  canvas.SetPointer({});
  CHECK(!canvas.Toggle(font, "SOUND", sound, area) && !sound);  // not clicked: unchanged
}

// A ShaderMaterial's params: stored by name, set again = replaced (no GPU needed).
static void TestMaterialParams() {
  auto material = wlw::rendering::ShaderMaterial::Create(nullptr);  // no shader: no GPU needed
  CHECK(!material->IsValid() && material->Params().empty() && material->AsShaderMaterial() == material.get());
  material->SetParam("uTime", 1.5f);
  material->SetParam("uTint", glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
  material->SetParam("uTime", 2.5f);
  CHECK(material->Params().size() == 2);
  const auto* time = material->GetParam("uTime");
  CHECK(time && std::holds_alternative<float>(*time) && Near(std::get<float>(*time), 2.5f));
  CHECK(material->GetParam("uMissing") == nullptr);
}

// A ShaderRect is one quad in a run of its own, with uv 0..1, between the runs around it.
static void TestCanvasShaderRect() {
  Canvas canvas;
  canvas.BeginFrame({400.0f, 800.0f}, {}, {400.0f, 800.0f});
  auto material = wlw::rendering::ShaderMaterial::Create(nullptr);  // no shader: no GPU needed
  canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1, 1, 1, 1});
  canvas.ShaderRect({{20.0f, 30.0f}, {120.0f, 80.0f}}, material, {1.0f, 0.0f, 0.0f, 1.0f});
  canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1, 1, 1, 1});
  canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1, 1, 1, 1});
  canvas.ShaderRect({{0.0f, 0.0f}, {1.0f, 1.0f}}, nullptr);  // no material: nothing
  const auto& batches = canvas.Batches();
  CHECK(batches.size() == 3 && canvas.QuadCount() == 4);
  CHECK(!batches[0].material && batches[0].quad_count == 1);
  CHECK(batches[1].material == material && batches[1].first_quad == 1 && batches[1].quad_count == 1);
  CHECK(!batches[2].material && batches[2].first_quad == 2 && batches[2].quad_count == 2);
  const auto& v = canvas.Vertices();
  CHECK(Near(v[4].position.x, 20.0f) && Near(v[4].position.y, 30.0f) && Near(v[6].position.x, 120.0f) &&
        Near(v[6].position.y, 80.0f));
  CHECK(Near(v[4].uv.x, 0.0f) && Near(v[4].uv.y, 0.0f) && Near(v[6].uv.x, 1.0f) && Near(v[6].uv.y, 1.0f));
  CHECK(Near(v[5].color.r, 1.0f) && Near(v[5].color.g, 0.0f));
}

int main() {
  auto font = LoadTestFont();
  if (font) {
    TestFontBakes(*font);
    TestMeasure(*font);
    TestLayout(*font);
    TestOutlineEdge(*font);
    TestCanvasTextAndBatches(*font);
    TestLayoutAndToggle(*font);
  }
  TestCanvasUnits();
  TestCanvasLine();
  TestMaterialParams();
  TestCanvasShaderRect();
  return TestResult("ui");
}
