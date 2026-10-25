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
  for (int i = 1; i <= static_cast<int>(Icon::Rotate); ++i) {
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

TEST_SUITE_END();
