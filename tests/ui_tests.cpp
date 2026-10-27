// Tests for the UI layer's CPU side: font baking, text measuring/layout, Canvas units and batching,
// the safe area, widgets
// (incl. ShaderRect), a ShaderMaterial's params.
// (Drawing itself needs a GPU; these check everything that's computed before it.)
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "test_helpers.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"
#include "wlw/ui/layout.h"
#include "wlw/ui/ui_context.h"
#include "wlw/ui/widgets.h"

TEST_SUITE_BEGIN("ui");

using namespace wlw::ui;

// The test font, loaded once per run (several cases use it; baking its atlas is not free).
static const Font& TestFont() {
  static const std::shared_ptr<Font> font = Font::Load(WLW_TEST_FONT);  // path set by tests/CMakeLists.txt
  REQUIRE(font.get() != nullptr);
  return *font;
}

TEST_CASE("font bakes") {
  const Font& font = TestFont();
  const Font::Atlas& atlas = font.GetAtlas();
  CHECK((atlas.width == 512 && atlas.height > 0));
  CHECK(atlas.pixels.size() == static_cast<size_t>(atlas.width) * atlas.height);
  bool has_inside = false;  // SDF: some texels are inside a glyph (> 0.5 = 128)
  for (uint8_t p : atlas.pixels) has_inside = has_inside || p > 200;
  CHECK(has_inside);
  CHECK((font.Ascent(100.0f) > 0.0f && font.Descent(100.0f) > 0.0f));
  CHECK(Near(font.Ascent(100.0f) + font.Descent(100.0f), 100.0f, 0.5f));  // size = line height
}

TEST_CASE("measure") {
  const Font& font = TestFont();
  CHECK(font.Measure("", 40.0f) == 0.0f);
  const float w20 = font.Measure("Score 123", 20.0f);
  const float w40 = font.Measure("Score 123", 40.0f);
  CHECK((w20 > 0.0f && Near(w40, 2.0f * w20, 1e-3f)));                                              // linear in size
  CHECK(font.Measure("AV", 40.0f) <= font.Measure("A", 40.0f) + font.Measure("V", 40.0f) + 1e-4f);  // kerning
  CHECK(font.Measure("WWW", 40.0f) > font.Measure("iii", 40.0f));
}

TEST_CASE("layout") {
  const Font& font = TestFont();
  std::vector<GlyphQuad> quads;
  const glm::vec2 pen(10.0f, 100.0f);
  font.Layout("A g", 40.0f, pen, quads);
  CHECK(quads.size() == 2);  // the space has no quad, only an advance
  // Left to right. (Quads overlap a little: each includes the distance field's padding.)
  CHECK((quads[0].min.x + quads[0].max.x) < (quads[1].min.x + quads[1].max.x));
  CHECK(quads[0].min.y < pen.y);  // 'A' is above the baseline...
  CHECK(quads[1].max.y > pen.y);  // ...'g' hangs below it
  for (const GlyphQuad& q : quads) CHECK((q.uv_min.x < q.uv_max.x && q.uv_max.y <= 1.0f));

  quads.clear();
  font.Layout("\xC3\xA9", 40.0f, pen, quads);  // UTF-8 'e acute': 2 non-ASCII bytes -> 2 '?'
  CHECK(quads.size() == 2);
}

TEST_CASE("outline edge") {
  const Font& font = TestFont();
  CHECK(Near(font.EdgeForOutline(0.0f), 0.5f));
  CHECK(font.EdgeForOutline(0.05f) < 0.5f);
  CHECK(Near(font.EdgeForOutline(10.0f), 0.02f));  // can't go beyond the baked padding
}

TEST_CASE("canvas units") {
  Canvas canvas;
  wlw::platform::SafeAreaInsets insets;
  insets.top = 100.0f;    // status bar / camera cutout
  insets.bottom = 54.0f;  // gesture bar
  canvas.Internal().BeginFrame({1080.0f, 2400.0f}, {400.0f, 720.0f});
  CHECK(Near(canvas.PixelsPerUnit(), 2.7f));  // the width limits: 1080 / 400
  CHECK((Near(canvas.Size().x, 400.0f) && Near(canvas.Size().y, 2400.0f / 2.7f, 1e-2f)));
  // The safe area (the UI layer's): the insets, converted to canvas units.
  const Rect safe = SafeAreaFor(canvas, insets);
  CHECK((Near(safe.min.y, 100.0f / 2.7f, 1e-3f) && Near(safe.min.x, 0.0f)));
  CHECK((Near(safe.max.y, (2400.0f - 54.0f) / 2.7f, 1e-2f) && Near(safe.max.x, 400.0f)));
  CHECK(Near(canvas.FromPixels({270.0f, 0.0f}).x, 100.0f));

  canvas.Internal().BeginFrame({2400.0f, 1080.0f}, {400.0f, 720.0f});  // landscape: the height limits
  CHECK(Near(canvas.PixelsPerUnit(), 1.5f));
}

TEST_CASE("canvas text and batches") {
  const Font& font_a = TestFont();
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});

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
  CHECK((canvas.Batches().size() == 1 && canvas.Batches()[0].quad_count == 6));
  auto font_b = Font::Load(WLW_TEST_FONT);  // same file, but a separate atlas
  canvas.Text(*font_b, "6", {100.0f, 150.0f}, 20.0f);
  CHECK((canvas.Batches().size() == 2 && canvas.Batches()[1].first_quad == 6));

  // Outline and shadow are extra copies of the glyphs, drawn first (behind).
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  TextStyle style;
  style.outline = 0.06f;
  style.shadow_offset = {2.0f, 3.0f};
  canvas.Text(font_a, "7", {100.0f, 100.0f}, 40.0f, Align::Left, style);
  CHECK(canvas.QuadCount() == 3);
  CHECK(canvas.Vertices()[0].params.y < 0.5f);   // shadow uses the outline's edge
  CHECK(canvas.Vertices()[8].params.y == 0.5f);  // fill: the normal edge
  // The field's range in atlas pixels, for the shader's anti-aliasing: 0 -> 1 over ~2 x padding (6).
  CHECK(canvas.Vertices()[8].params.z == font_a.DistanceRange());
  CHECK(Near(font_a.DistanceRange(), 255.0f * 6.0f / 128.0f, 1e-3f));

  // Rounded rect: half size and a radius clamped to fit.
  canvas.FillRect({{0, 0}, {40, 10}}, {1, 1, 1, 1}, 50.0f);
  const auto& rect = canvas.Vertices().back();
  CHECK(
      (rect.params.x == 2.0f && Near(rect.params.y, 20.0f) && Near(rect.params.z, 5.0f) && Near(rect.params.w, 5.0f)));
}

TEST_CASE("canvas turned text") {
  const Font& font = TestFont();
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  const glm::vec2 at(200.0f, 100.0f);
  canvas.Text(font, "HH", at, 40.0f, Align::Center);
  const std::vector<UIVertex> flat = canvas.Vertices();

  // A quarter turn clockwise (y down): what was right of the point is now below it; uvs and params are untouched.
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  TextStyle style;
  style.rotation = 1.5707963f;
  canvas.Text(font, "HH", at, 40.0f, Align::Center, style);
  const auto& turned = canvas.Vertices();
  REQUIRE(turned.size() == flat.size());
  for (size_t i = 0; i < flat.size(); ++i) {
    const glm::vec2 d = flat[i].position - at;
    CHECK(Near(turned[i].position.x, at.x - d.y, 1e-3f));
    CHECK(Near(turned[i].position.y, at.y + d.x, 1e-3f));
    CHECK((turned[i].uv == flat[i].uv && turned[i].params == flat[i].params));
  }

  // The shadow turns with the text but still falls by its offset on screen.
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  style.shadow_offset = {0.0f, 3.0f};
  canvas.Text(font, "HH", at, 40.0f, Align::Center, style);
  const auto& shadowed = canvas.Vertices();
  REQUIRE(shadowed.size() == flat.size() * 2);
  for (size_t i = 0; i < flat.size(); ++i) {
    CHECK(Near(glm::length(shadowed[i].position - shadowed[i + flat.size()].position - glm::vec2(0.0f, 3.0f)), 0.0f,
               1e-3f));
  }
}

// A glyph's box from its 4 corners (min, max), for the per-glyph tests.
static std::pair<glm::vec2, glm::vec2> QuadBounds(const std::vector<UIVertex>& v, size_t quad) {
  glm::vec2 min(1e9f), max(-1e9f);
  for (size_t i = quad * 4; i < quad * 4 + 4; ++i) {
    min = glm::min(min, v[i].position);
    max = glm::max(max, v[i].position);
  }
  return {min, max};
}

TEST_CASE("canvas glyph effects") {
  const Font& font = TestFont();
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  const glm::vec2 at(200.0f, 100.0f);
  canvas.Text(font, "H H", at, 40.0f, Align::Center);
  const std::vector<UIVertex> flat = canvas.Vertices();
  REQUIRE(flat.size() == 8);

  // What the effect is told: the character index (the space has no glyph), the order among glyphs, the count, and
  // a centre at the line's vertical middle. It runs once per glyph, however many layers the style draws.
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  std::vector<GlyphFx> seen;
  canvas.Text(font, "H H", at, 40.0f, Align::Center, TextStyle::Outlined({1, 1, 1, 1}),
              [&](GlyphFx& fx) { seen.push_back(fx); });
  REQUIRE(seen.size() == 2);
  CHECK((seen[0].index == 0 && seen[1].index == 2 && seen[0].order == 0 && seen[1].order == 1));
  CHECK((seen[0].count == 2 && Near(seen[0].center.y, at.y) && seen[0].center.x < at.x && seen[1].center.x > at.x));
  const auto [h_min, h_max] = QuadBounds(flat, 0);
  CHECK(Near(seen[0].center.x, (h_min.x + h_max.x) * 0.5f, 1e-3f));

  // Offset moves only its glyph; scale grows it about its centre; the tint multiplies its colour.
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  canvas.Text(font, "H H", at, 40.0f, Align::Center, {}, [](GlyphFx& fx) {
    if (fx.order == 1) {
      fx.offset = {0.0f, -10.0f};
      fx.scale = 2.0f;
      fx.tint = {1.0f, 1.0f, 1.0f, 0.5f};
    }
  });
  const auto& moved = canvas.Vertices();
  for (size_t i = 0; i < 4; ++i) CHECK((moved[i].position == flat[i].position && moved[i].color == flat[i].color));
  const auto [a_min, a_max] = QuadBounds(flat, 1);
  const auto [b_min, b_max] = QuadBounds(moved, 1);
  // (Twice the size, give or take the trim: the margin kept for anti-aliasing is in screen pixels.)
  CHECK(Near(b_max.x - b_min.x, (a_max.x - a_min.x) * 2.0f, 1.5f));
  CHECK(Near((b_min.x + b_max.x) * 0.5f, seen[1].center.x, 1e-3f));
  CHECK(Near(b_min.y + 10.0f, at.y - (at.y - a_min.y) * 2.0f, 1.5f));  // about the line middle, then up 10
  CHECK(Near(moved[4].color.a, 0.5f));

  // A quarter turn of one glyph about its centre: its corners go round, its uvs stay.
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  canvas.Text(font, "H H", at, 40.0f, Align::Center, {}, [](GlyphFx& fx) { fx.rotation = 1.5707963f; });
  const auto& spun = canvas.Vertices();
  for (size_t i = 0; i < 8; ++i) {
    const glm::vec2 c = seen[i / 4].center, d = flat[i].position - c;
    CHECK((Near(spun[i].position.x, c.x - d.y, 1e-3f) && Near(spun[i].position.y, c.y + d.x, 1e-3f)));
    CHECK(spun[i].uv == flat[i].uv);
  }
}

TEST_CASE("text gradient fill") {
  const wlw::Color red{1, 0, 0, 1}, clear{0, 0, 0, 0}, blue{0, 0, 1, 1};
  // Mixed premultiplied: halfway from red to transparent is still red, at half alpha (no dark middle).
  const LinearGradient fade = LinearGradient::Vertical({{0.0f, red}, {1.0f, clear}});
  CHECK(fade.At(0.5f) == wlw::Color(1.0f, 0.0f, 0.0f, 0.5f));
  CHECK((fade.At(-1.0f) == red && fade.At(2.0f) == clear && LinearGradient{}.At(0.3f).a == 0.0f));

  const Font& font = TestFont();
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  TextStyle style;
  style.fill = LinearGradient::Vertical({{0.5f, red}, {1.0f, blue}});
  canvas.Text(font, "H", {200.0f, 100.0f}, 40.0f, Align::Center, style);
  // The 'H' spans the line's middle (y = 100), where the gradient has a stop: strips meet there. Above it the fill
  // is red, below it turns blue; strips run across, so each strip's top corners agree, and so do its bottom ones.
  const auto& v = canvas.Vertices();
  REQUIRE(canvas.QuadCount() >= 2);
  bool cut_at_middle = false;
  for (size_t i = 0; i < v.size(); ++i) {
    cut_at_middle = cut_at_middle || Near(v[i].position.y, 100.0f, 1e-3f);
    if (v[i].position.y <= 100.0f + 1e-3f) CHECK(v[i].color == red);
    if (v[i].position.y > 101.0f) CHECK(v[i].color.b > 0.0f);
    if (i % 4 == 0) CHECK((v[i].color == v[i + 1].color && v[i + 2].color == v[i + 3].color));
    if (i % 4 == 0 && i > 0) CHECK(v[i].uv.y == v[i - 1].uv.y);  // the atlas is cut where the strips meet
  }
  CHECK(cut_at_middle);
  CHECK(canvas.Batches().size() == 1);

  // Horizontal: left to right across the whole line, so each glyph gets its part of the ramp.
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  style.fill = LinearGradient::Horizontal({{0.0f, red}, {1.0f, blue}});
  canvas.Text(font, "HH", {200.0f, 100.0f}, 40.0f, Align::Center, style);
  for (const UIVertex& a : v) {
    for (const UIVertex& b : v) {
      if (a.position.x < b.position.x - 1e-3f) CHECK(a.color.b <= b.color.b + 1e-5f);  // bluer to the right
    }
  }
  CHECK(v.front().color.r > 0.5f);
}

TEST_CASE("text glow") {
  const Font& font = TestFont();
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  TextStyle style = TextStyle::Outlined({1, 1, 1, 1});
  style.glow = {.color = {1.0f, 0.8f, 0.2f, 1.0f}, .width = 0.08f};
  canvas.Text(font, "H", {200.0f, 100.0f}, 40.0f, Align::Center, style);
  // Glow, shadow, outline, fill: the glow first, its own additive run, cut further out than the outline and soft.
  REQUIRE(canvas.QuadCount() == 4);
  const auto& v = canvas.Vertices();
  const float far = font.EdgeForOutline(0.08f);
  CHECK((Near(v[0].params.y, (0.5f + far) * 0.5f) && Near(v[0].params.w, (0.5f - far) * 0.5f)));
  CHECK((v[4].params.w == 0.0f && v[12].params.w == 0.0f));  // the rest stay crisp
  REQUIRE(canvas.Batches().size() == 2);
  CHECK((canvas.Batches()[0].blend == wlw::rendering::BlendMode::Additive && canvas.Batches()[0].quad_count == 1));
  CHECK(canvas.Batches()[1].blend == wlw::rendering::BlendMode::Alpha);

  // Blended like a soft shadow: no extra run. No colour (alpha 0) or no width: no glow.
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  style.glow.additive = false;
  canvas.Text(font, "H", {200.0f, 100.0f}, 40.0f, Align::Center, style);
  CHECK((canvas.QuadCount() == 4 && canvas.Batches().size() == 1));
  style.glow.width = 0.0f;
  canvas.Text(font, "H", {200.0f, 100.0f}, 40.0f, Align::Center, style);
  CHECK(canvas.QuadCount() == 7);
}

TEST_CASE("text layers trimmed to their reach") {
  // A font with a wide field margin (12 atlas pixels): the glow uses all of it, the outline a few pixels, the fill
  // only room for anti-aliasing. Each layer's quad is cut down to that, its atlas window with it.
  const auto font = Font::Load(WLW_TEST_FONT, {.bake_size = 48.0f, .padding = 12});
  REQUIRE(font != nullptr);
  CHECK((font->Padding() == 12.0f && font->BakeSize() == 48.0f && font->Reach(0.5f) == 0.0f));
  CHECK(Near(font->Reach(font->EdgeForOutline(0.1f)), 0.1f * 48.0f, 1e-3f));  // the outline's width, in texels
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  TextStyle style = TextStyle::Outlined({1, 1, 1, 1});
  style.shadow_offset = {};
  style.glow = {.color = {1, 1, 1, 1}, .width = 0.3f};
  canvas.Text(*font, "H", {200.0f, 100.0f}, 48.0f, Align::Center, style);
  REQUIRE(canvas.QuadCount() == 3);  // glow, outline, fill
  const auto& v = canvas.Vertices();
  const auto width = [&](size_t quad) { return QuadBounds(v, quad).second.x - QuadBounds(v, quad).first.x; };
  CHECK((width(0) > width(1) && width(1) > width(2)));
  // At 1 unit per texel and 1 pixel per unit: the glow keeps the whole margin, the fill 2.5 of its 12 texels a side.
  CHECK(Near(width(0) - width(2), 2.0f * (12.0f - 2.5f), 1e-3f));
  // The uvs shrink in step: the same atlas texel stays under the same point.
  const float uv_per_unit = (v[1].uv.x - v[0].uv.x) / (v[1].position.x - v[0].position.x);
  CHECK(Near((v[9].uv.x - v[8].uv.x) / (v[9].position.x - v[8].position.x), uv_per_unit, 1e-5f));
  CHECK(Near(v[8].uv.x - v[0].uv.x, uv_per_unit * (v[8].position.x - v[0].position.x), 1e-5f));
}

TEST_CASE("canvas keeps its fonts alive") {
  // Text from a font the caller lets go of before the frame is drawn still has its atlas.
  Canvas canvas;
  canvas.Internal().BeginFrame({800.0f, 600.0f}, {800.0f, 600.0f});
  std::weak_ptr<Font> watch;
  {
    std::shared_ptr<Font> font = Font::Load(WLW_TEST_FONT);
    REQUIRE(font != nullptr);
    watch = font;
    canvas.Text(*font, "kept", {10.0f, 10.0f}, 20.0f);
  }
  CHECK((!watch.expired() && canvas.Batches().size() == 1 && canvas.Batches()[0].font == watch.lock()));
  canvas.Internal().BeginFrame({800.0f, 600.0f}, {800.0f, 600.0f});  // the next frame lets it go
  CHECK(watch.expired());

  // A uniquely owned font (what Load returns) works too: the batch points at it without owning it.
  std::unique_ptr<Font> unique = Font::Load(WLW_TEST_FONT);
  REQUIRE(unique != nullptr);
  canvas.Text(*unique, "plain", {10.0f, 10.0f}, 20.0f);
  CHECK((canvas.Batches().size() == 1 && canvas.Batches()[0].font.get() == unique.get()));
  CHECK(canvas.Batches()[0].font.use_count() == 0);  // owns nothing
}

TEST_CASE("canvas line") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  canvas.Line({10.0f, 20.0f}, {30.0f, 20.0f}, 4.0f, {1, 1, 1, 1});  // horizontal, 20 long
  CHECK(canvas.QuadCount() == 1);
  const auto& v = canvas.Vertices();
  // A capsule: half length 10 + 2 (the round ends), half thickness 2, radius 2.
  CHECK((Near(v[0].params.y, 12.0f) && Near(v[0].params.z, 2.0f) && Near(v[0].params.w, 2.0f)));
  CHECK((Near(v[0].position.x, 8.0f) && Near(v[0].position.y, 18.0f) && Near(v[2].position.x, 32.0f)));

  canvas.Line({0.0f, 0.0f}, {30.0f, 40.0f}, 2.0f, {1, 1, 1, 1});  // diagonal, 50 long
  for (size_t i = 4; i < 8; ++i) {
    // Every corner is the same distance from the centre (15, 20): a rotated rectangle.
    CHECK(Near(glm::length(v[i].position - glm::vec2(15.0f, 20.0f)), std::sqrt(26.0f * 26.0f + 1.0f), 1e-3f));
    CHECK((Near(std::fabs(v[i].uv.x), 26.0f) && Near(std::fabs(v[i].uv.y), 1.0f)));  // uv in the line's own frame
  }
  canvas.Line({5.0f, 5.0f}, {5.0f, 5.0f}, 2.0f, {1, 1, 1, 1});  // zero length: nothing
  CHECK(canvas.QuadCount() == 2);
}

TEST_CASE("layout and toggle") {
  const Font& font = TestFont();
  const Rect area{{0.0f, 0.0f}, {100.0f, 50.0f}};
  CHECK((Near(Centered({10.0f, 20.0f}, {4.0f, 6.0f}).min.x, 8.0f) &&
         Near(Centered({10.0f, 20.0f}, {4.0f, 6.0f}).max.y, 23.0f)));
  CHECK((Near(Inset(area, 5.0f).min.x, 5.0f) && Near(Inset(area, 5.0f).max.y, 45.0f)));
  const Rect last_row = Row(area, 1, 2, 10.0f);  // two rows of 20 with a 10 gap
  CHECK((Near(last_row.min.y, 30.0f) && Near(last_row.max.y, 50.0f) && Near(last_row.max.x, 100.0f)));
  const Rect middle = Column(area, 1, 3, 5.0f);  // three columns of 30 with 5 gaps
  CHECK((Near(middle.min.x, 35.0f) && Near(middle.max.x, 65.0f)));

  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  UIContext ui(canvas);
  ui.pointer.position = {50.0f, 25.0f};
  ui.pointer.pressed = ui.pointer.released = true;
  bool sound = true;
  CHECK((Toggle(ui, font, "SOUND", sound, area) && !sound));  // clicked: flipped
  ui.pointer = {};
  CHECK((!Toggle(ui, font, "SOUND", sound, area) && !sound));  // not clicked: unchanged
}

// Triangles and arcs: what the shader gets to compute their coverage.
TEST_CASE("canvas shapes") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});  // 1 pixel per unit
  canvas.Triangle({0.0f, 0.0f}, {30.0f, 0.0f}, {0.0f, 40.0f}, {1, 1, 1, 1});
  CHECK(canvas.QuadCount() == 1);
  const auto& v = canvas.Vertices();
  CHECK(v[0].params.x == static_cast<float>(QuadKind::Triangle));
  CHECK(v[2].position == v[3].position);  // the quad's 2nd half is empty
  // Grown by 1.5 pixels: each corner is 1.5 outside both its edges (distance -1.5), and the
  // distance to the opposite edge is positive.
  CHECK((Near(v[0].uv.y, -1.5f, 1e-3f) && Near(v[0].params.y, -1.5f, 1e-3f) && v[0].uv.x > 20.0f));
  CHECK((Near(v[1].uv.x, -1.5f, 1e-3f) && Near(v[1].params.y, -1.5f, 1e-3f) && v[1].uv.y > 20.0f));
  CHECK((v[0].position.x < 0.0f && v[0].position.y < 0.0f));
  // Same triangle the other way round: the same distances (winding doesn't matter).
  canvas.Triangle({0.0f, 0.0f}, {0.0f, 40.0f}, {30.0f, 0.0f}, {1, 1, 1, 1});
  CHECK((Near(v[4].uv.x, v[0].uv.x, 1e-3f) || Near(v[4].uv.y, v[0].uv.x, 1e-3f) ||
         Near(v[4].params.y, v[0].uv.x, 1e-3f)));
  canvas.Triangle({0.0f, 0.0f}, {10.0f, 10.0f}, {20.0f, 20.0f}, {1, 1, 1, 1});  // flat: nothing
  CHECK(canvas.QuadCount() == 2);

  // An arc: radius, half thickness, half sweep; the square turned so +y in uv is its middle.
  canvas.Arc({100.0f, 100.0f}, 20.0f, 4.0f, 0.0f, 3.14159265f, {1, 1, 1, 1});  // the right half
  const auto& a = v[8];
  CHECK(a.params.x == static_cast<float>(QuadKind::Arc));
  CHECK((Near(a.params.y, 20.0f) && Near(a.params.z, 2.0f) && Near(a.params.w, 3.14159265f / 2.0f)));
  // The middle of this arc points at 3 o'clock: uv (0, r) is at (100 + r, 100).
  glm::vec2 center(0.0f), axis(0.0f);
  for (int i = 8; i < 12; ++i) center += v[i].position * 0.25f;
  CHECK((Near(center.x, 100.0f, 1e-3f) && Near(center.y, 100.0f, 1e-3f)));
  axis = (v[10].position - v[9].position) / (v[10].uv.y - v[9].uv.y);  // corner 1 -> 2 moves along uv y
  CHECK((Near(axis.x, 1.0f, 1e-3f) && Near(axis.y, 0.0f, 1e-3f)));
  canvas.Ring({0.0f, 0.0f}, 10.0f, 2.0f, {1, 1, 1, 1});
  CHECK(Near(v[12].params.w, 3.14159265f));  // a whole ring: half sweep = pi
  canvas.Circle({50.0f, 50.0f}, 8.0f, {1, 1, 1, 1});
  CHECK((v[16].params.x == static_cast<float>(QuadKind::Rect) && Near(v[16].params.w, 8.0f)));
}

// What widgets remember: kept while asked for every frame, dropped a frame after they stop.
TEST_CASE("widget memory") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  UIContext ui(canvas);
  ui.NewFrame(0.016f);
  ui.Memory(7, 1.0f) += 1.0f;
  CHECK((Near(ui.Memory(7), 2.0f) && ui.MemorySize() == 1));
  ui.NewFrame(0.016f);
  CHECK(Near(ui.Memory(7, 5.0f), 2.0f));  // still there: asked for last frame
  ui.NewFrame(0.016f);                    // not asked for this time...
  ui.NewFrame(0.016f);                    // ...so it's gone
  CHECK((ui.MemorySize() == 0 && Near(ui.Memory(7, 5.0f), 5.0f)));
}

// A click is a press and a release on the same widget; sliding off cancels.
TEST_CASE("pressable") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  UIContext ui(canvas);
  const Rect rect{{0.0f, 0.0f}, {100.0f, 50.0f}};
  ui.pointer.position = {50.0f, 25.0f};
  ui.pointer.pressed = ui.pointer.down = true;
  Press p = Pressable(ui, 42, rect);
  CHECK((!p.clicked && p.held && p.amount >= 0.5f));
  ui.pointer = {};
  ui.pointer.position = {300.0f, 25.0f};  // slid off and let go
  ui.pointer.released = true;
  CHECK(!Pressable(ui, 42, rect).clicked);
  ui.pointer = {{50.0f, 25.0f}, true, true, false};
  Pressable(ui, 42, rect);
  ui.pointer = {{50.0f, 25.0f}, false, false, true};
  CHECK(Pressable(ui, 42, rect).clicked);
  ui.interactive = false;  // screens not on top don't respond
  ui.pointer = {{50.0f, 25.0f}, true, true, true};
  CHECK(!Pressable(ui, 42, rect).clicked);
}

TEST_CASE("themed widgets") {
  const Font& font = TestFont();
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  UIContext ui(canvas);
  ui.theme.font = &font;
  ui.safe_area = {{0.0f, 0.0f}, canvas.Size()};
  const auto tap = [&ui](glm::vec2 at) { ui.pointer = {at, false, true, true}; };

  // A switch flips on a tap; its knob slides over a few frames.
  const Rect sw{{10.0f, 10.0f}, {70.0f, 40.0f}};
  bool music = false;
  ui.NewFrame(0.0f);
  tap(sw.Center());
  CHECK((Switch(ui, music, sw) && music));
  ui.pointer = {};
  ui.NewFrame(0.05f);
  CHECK((!Switch(ui, music, sw) && music));

  // A setting row is tappable anywhere on it.
  const Rect row{{10.0f, 100.0f}, {300.0f, 156.0f}};
  bool sound = true;
  tap({30.0f, 120.0f});
  CHECK((SettingRow(ui, Icon::Sound, "SOUND", sound, row) && !sound));

  // Tabs: tapping the second one selects it; tapping the selected one changes nothing.
  const std::string_view labels[] = {"RUN", "RUSH"};
  const Rect tabs{{0.0f, 200.0f}, {200.0f, 240.0f}};
  int selected = 0;
  tap({150.0f, 220.0f});
  CHECK((Segmented(ui, labels, selected, tabs) && selected == 1));
  tap({150.0f, 220.0f});
  CHECK((!Segmented(ui, labels, selected, tabs) && selected == 1));

  // A card reports its click and where its picture goes (inside the card, in its top part).
  const Rect card{{0.0f, 300.0f}, {120.0f, 450.0f}};
  tap(card.Center());
  const CardResult result = Card(ui, "PUZZLES", "LEVEL 3", card, {0.2f, 0.6f, 0.6f, 1.0f}, "NEW");
  CHECK((result.clicked && result.art.min.y > card.min.y && result.art.max.y < card.Center().y + 20.0f));

  // A dialog: content under its title; a tap outside closes it once it's in, the button anytime.
  ui.pointer = {};
  ui.NewFrame(0.0f);
  DialogResult dialog = Dialog(ui, "SETTINGS", {300.0f, 400.0f});
  CHECK((dialog.content.min.y > ui.safe_area.Center().y - 200.0f && !dialog.close));
  ui.NewFrame(1.0f);  // fully in
  Dialog(ui, "SETTINGS", {300.0f, 400.0f});
  ui.NewFrame(0.016f);
  tap({5.0f, 5.0f});
  CHECK(Dialog(ui, "SETTINGS", {300.0f, 400.0f}).close);
  tap({200.0f, 400.0f});  // inside the panel
  CHECK(!Dialog(ui, "SETTINGS", {300.0f, 400.0f}).close);
  tap({200.0f + 150.0f - 14.0f, 400.0f - 200.0f + 14.0f});  // the close button on its corner
  CHECK(Dialog(ui, "SETTINGS", {300.0f, 400.0f}, true).close);

  // Everything drew something; icons are only shapes (no text), every one of them.
  const size_t before = canvas.QuadCount();
  for (int i = 1; i <= static_cast<int>(Icon::Motion); ++i) {
    const size_t at = canvas.QuadCount();
    DrawIcon(canvas, static_cast<Icon>(i), {{0.0f, 0.0f}, {48.0f, 48.0f}}, {1, 1, 1, 1});
    CHECK(canvas.QuadCount() > at);
  }
  CHECK((canvas.QuadCount() > before && canvas.Batches().size() >= 1));
  CHECK(Chip(ui, "BEST 100", {200.0f, 600.0f}, Icon::Trophy).Size().x > font.Measure("BEST 100", 17.0f));
}

TEST_CASE("badge") {
  const Font& font = TestFont();
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  UIContext ui(canvas);
  ui.theme.font = &font;
  // Draws a badge at (200, 100), radius 40; returns its square and how many quads it took.
  const auto draw = [&](std::string_view value, std::string_view caption, float progress, BadgeStyle style = {}) {
    const size_t before = canvas.QuadCount();
    const Rect rect = Badge(ui, {200.0f, 100.0f}, 40.0f, value, caption, progress, style);
    return std::pair{rect, canvas.QuadCount() - before};
  };

  // The disc's square, scaled about its centre.
  const auto [rect, plain] = draw("3", "", -1.0f);
  CHECK((rect.min == glm::vec2(160.0f, 60.0f) && rect.max == glm::vec2(240.0f, 140.0f)));
  BadgeStyle bigger;
  bigger.scale = 1.5f;
  CHECK(Near(draw("3", "", -1.0f, bigger).first.Size().x, 120.0f));

  // A caption and a progress ring each add shapes; an empty ring still shows its track.
  CHECK(draw("3", "STAGE", -1.0f).second > plain);
  const size_t empty_ring = draw("3", "", 0.0f).second;
  CHECK(empty_ring > plain);
  CHECK(draw("3", "", 0.5f).second > empty_ring);

  // A long value is shrunk to stay inside the ring (text quads: kind 1).
  const size_t at = canvas.Vertices().size();
  draw("12345678", "", 0.5f);
  float right = 0.0f;
  for (size_t i = at; i < canvas.Vertices().size(); ++i) {
    if (canvas.Vertices()[i].params.x == 1.0f) right = std::max(right, canvas.Vertices()[i].position.x);
  }
  CHECK((right > 210.0f && right < 236.0f));
}

// A ShaderMaterial's params: stored by name, set again = replaced (no GPU needed).
TEST_CASE("material params") {
  std::shared_ptr<wlw::rendering::ShaderMaterial> material = wlw::rendering::ShaderMaterial::Create(nullptr);  // no GPU
  CHECK((!material->IsValid() && material->Params().empty() && material->AsShaderMaterial() == material.get()));
  material->SetParam("uTime", 1.5f);
  material->SetParam("uTint", glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
  material->SetParam("uTime", 2.5f);
  CHECK(material->Params().size() == 2);
  const auto* time = material->GetParam("uTime");
  CHECK((time && std::holds_alternative<float>(*time) && Near(std::get<float>(*time), 2.5f)));
  CHECK(material->GetParam("uMissing") == nullptr);
}

// A ShaderRect is one quad in a run of its own, with uv 0..1, between the runs around it.
TEST_CASE("canvas shader rect") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  std::shared_ptr<wlw::rendering::ShaderMaterial> material = wlw::rendering::ShaderMaterial::Create(nullptr);  // no GPU
  canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1, 1, 1, 1});
  canvas.ShaderRect({{20.0f, 30.0f}, {120.0f, 80.0f}}, material, {1.0f, 0.0f, 0.0f, 1.0f});
  canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1, 1, 1, 1});
  canvas.FillRect({{0.0f, 0.0f}, {10.0f, 10.0f}}, {1, 1, 1, 1});
  canvas.ShaderRect({{0.0f, 0.0f}, {1.0f, 1.0f}}, nullptr);  // no material: nothing
  const auto& batches = canvas.Batches();
  CHECK((batches.size() == 3 && canvas.QuadCount() == 4));
  CHECK((!batches[0].material && batches[0].quad_count == 1));
  CHECK((batches[1].material == material && batches[1].first_quad == 1 && batches[1].quad_count == 1));
  CHECK((!batches[2].material && batches[2].first_quad == 2 && batches[2].quad_count == 2));
  const auto& v = canvas.Vertices();
  CHECK((Near(v[4].position.x, 20.0f) && Near(v[4].position.y, 30.0f) && Near(v[6].position.x, 120.0f) &&
         Near(v[6].position.y, 80.0f)));
  CHECK((Near(v[4].uv.x, 0.0f) && Near(v[4].uv.y, 0.0f) && Near(v[6].uv.x, 1.0f) && Near(v[6].uv.y, 1.0f)));
  CHECK((Near(v[5].color.r, 1.0f) && Near(v[5].color.g, 0.0f)));
}

// A gradient: strips between its stops (and flat ends), each drawing its part of the WHOLE rounded rect.
TEST_CASE("canvas gradient") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  const wlw::Color red{1, 0, 0, 1}, clear{1, 1, 1, 0}, blue{0, 0, 1, 1};
  const Rect rect{{0.0f, 100.0f}, {200.0f, 300.0f}};  // 200 x 200, centre (100, 200)
  canvas.FillRect(rect, LinearGradient::Vertical({{0.25f, red}, {0.5f, clear}, {0.75f, blue}}), 12.0f);
  CHECK(canvas.QuadCount() == 4);  // flat top, red -> clear, clear -> blue, flat bottom
  CHECK(canvas.Batches().size() == 1);
  const auto& v = canvas.Vertices();
  for (const UIVertex& corner : v) {
    CHECK((corner.params.x == static_cast<float>(QuadKind::Rect) && Near(corner.params.y, 100.0f) &&
           Near(corner.params.z, 100.0f) && Near(corner.params.w, 12.0f)));
    CHECK(Near(corner.uv.y, corner.position.y - 200.0f));  // measured from the whole rect's centre
  }
  // The strips tile the rect: 100-150, 150-200, 200-250, 250-300.
  CHECK((Near(v[0].position.y, 100.0f) && Near(v[2].position.y, 150.0f) && Near(v[4].position.y, 150.0f) &&
         Near(v[10].position.y, 250.0f) && Near(v[14].position.y, 300.0f)));
  CHECK((v[0].color == red && v[3].color == red));  // flat top
  CHECK((v[4].color == red && v[5].color == red && v[6].color == clear && v[7].color == clear));
  CHECK((v[12].color == blue && v[14].color == blue));  // flat bottom

  // Stops at the ends: no flat strips. Horizontal: colours change left to right.
  canvas.FillRect(rect, LinearGradient::Horizontal({{0.0f, red}, {1.0f, blue}}));
  CHECK(canvas.QuadCount() == 5);
  CHECK((v[16].color == red && v[17].color == blue && v[18].color == blue && v[19].color == red));
  CHECK((Near(v[16].uv.x, -100.0f) && Near(v[17].uv.x, 100.0f)));
  canvas.FillRect(rect, LinearGradient::Vertical({{0.5f, red}}));  // one stop: a plain rect
  CHECK((canvas.QuadCount() == 6 && v[20].color == red && v[22].color == red));
  canvas.FillRect(rect, LinearGradient{});  // no stops: nothing
  CHECK(canvas.QuadCount() == 6);
}

// Modulate scopes multiply every colour drawn inside them, nest, and end with their scope.
TEST_CASE("canvas triangle corner colours") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  const wlw::Color red{1, 0, 0, 1}, green{0, 1, 0, 1}, clear{1, 1, 1, 0};
  canvas.Triangle({0, 0}, {100, 0}, {0, 100}, red, green, clear);  // a ray fading towards its tip
  const auto& v = canvas.Vertices();
  REQUIRE(v.size() == 4);
  CHECK((v[0].color == red && v[1].color == green && v[2].color == clear && v[3].color == clear));
  canvas.Triangle({0, 0}, {100, 0}, {0, 100}, red);  // one colour: all corners
  CHECK((v[4].color == red && v[7].color == red));
}

TEST_CASE("canvas modulate") {
  Canvas canvas;
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  const Rect rect{{0.0f, 0.0f}, {10.0f, 10.0f}};
  const wlw::Color gold{1.0f, 0.8f, 0.2f, 1.0f};
  const auto& v = canvas.Vertices();
  {
    auto fade = canvas.Fade(0.5f);
    canvas.FillRect(rect, gold);
    CHECK((Near(v[0].color.a, 0.5f) && Near(v[0].color.g, 0.8f)));
    {
      auto tint = canvas.Modulate({0.5f, 1.0f, 1.0f, 0.5f});
      canvas.Circle({5.0f, 5.0f}, 3.0f, gold);
      CHECK((Near(v[4].color.a, 0.25f) && Near(v[4].color.r, 0.5f)));
      canvas.Triangle({0, 0}, {10, 0}, {0, 10}, gold);
      const std::shared_ptr<wlw::rendering::WTexture2D> texture = wlw::rendering::WTexture2D::Create(1, 1);
      canvas.Image(rect, texture, {.tint = gold, .premultiplied = true});
      CHECK(v[12].params.y == 1.0f);  // a premultiplied texture: ui.frag doesn't multiply it again
      canvas.ShaderRect(rect, wlw::rendering::ShaderMaterial::Create(nullptr), gold);
      CHECK(canvas.QuadCount() == 5);
      for (size_t i = 8; i < 20; ++i) CHECK(Near(v[i].color.a, 0.25f));
      canvas.Image(rect, texture, {.tint = gold});
      CHECK(v[20].params.y == 0.0f);
    }
    CHECK(Near(canvas.CurrentModulate().a, 0.5f));  // the inner scope ended
  }
  CHECK(canvas.CurrentModulate() == wlw::Color(1, 1, 1, 1));
  canvas.FillRect(rect, gold);
  CHECK(v[24].color == gold);

  auto left_open = canvas.Fade(0.1f);  // a new frame starts unmodulated
  canvas.Internal().BeginFrame({400.0f, 800.0f}, {400.0f, 800.0f});
  CHECK(canvas.CurrentModulate() == wlw::Color(1, 1, 1, 1));
}

TEST_SUITE_END();
