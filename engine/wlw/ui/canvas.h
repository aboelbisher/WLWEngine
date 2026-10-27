#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/core/geometry.h"
#include "wlw/rendering/blend_mode.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/font.h"

namespace wlw::ui {

enum class Align { Left, Center, Right };

// Canvas rectangles are wlw::Rect (core/geometry.h): min = top-left, max = bottom-right in Canvas units (y down).
using Rect = wlw::Rect;

// How Canvas::Image draws its texture.
struct ImageStyle {
  Color tint{1.0f, 1.0f, 1.0f, 1.0f};   // multiplies the texture (white texture + tint = any colour)
  Rect uv{{0.0f, 0.0f}, {1.0f, 1.0f}};  // the part of the texture (sprite sheets); (0, 0) = first row uploaded
  rendering::BlendMode blend = rendering::BlendMode::Alpha;  // e.g. Additive for glows (see blend_mode.h)
  // The texture's colours are already multiplied by its alpha: a viewport's texture (everything the engine draws
  // is premultiplied). Drawn as straight alpha it would be multiplied twice: darker where it's see-through, a dark
  // fringe where filtering mixes its edges with the clear colour.
  bool premultiplied = false;
};

// A colour ramp for Canvas::FillRect and text fills: colours at positions 0..1 across the rect (0 = top or left),
// straight alpha, blended linearly between stops (in premultiplied space, so a fade to transparent doesn't darken).
// Before the first stop and after the last, their colours hold. Up to kMaxStops; more are ignored.
struct LinearGradient {
  enum class Direction { Vertical, Horizontal };  // top -> bottom, left -> right
  struct Stop {
    float at = 0.0f;  // 0..1, in increasing order
    Color color;
  };
  static constexpr size_t kMaxStops = 8;

  Direction direction = Direction::Vertical;
  std::array<Stop, kMaxStops> stops{};
  size_t count = 0;

  LinearGradient& Add(float at, const Color& color) {
    if (count < kMaxStops) stops[count++] = {at, color};
    return *this;
  }
  static LinearGradient Vertical(std::initializer_list<Stop> list) { return Of(Direction::Vertical, list); }
  static LinearGradient Horizontal(std::initializer_list<Stop> list) { return Of(Direction::Horizontal, list); }

  bool Empty() const { return count == 0; }
  // The colour at `t` (straight alpha), mixed in premultiplied space like the drawing does. Transparent white when
  // empty.
  Color At(float t) const;

 private:
  static LinearGradient Of(Direction direction, std::initializer_list<Stop> list) {
    LinearGradient gradient;
    gradient.direction = direction;
    for (const Stop& stop : list) gradient.Add(stop.at, stop.color);
    return gradient;
  }
};

// A soft light around text, from the same distance field as the outline: full at the glyph's edge, gone `width`
// out. Drawn first, behind the shadow and outline. How far it can reach is limited by the font's
// FontOptions::padding (a font loaded for big glowing titles wants more padding than the HUD's).
struct TextGlow {
  Color color{1.0f, 1.0f, 1.0f, 0.0f};  // alpha 0 = no glow
  float width = 0.15f;                  // as a fraction of the text size
  // Adds light (reads as a glow on dark backgrounds, and colours above 1 feed bloom on an HDR picture layer);
  // false blends it like a soft shadow. Additive glows are their own draw call.
  bool additive = true;
};

// How text looks. Glow, outline and shadow come from the same SDF atlas (see Font).
struct TextStyle {
  Color color{1.0f, 1.0f, 1.0f, 1.0f};
  // When it has stops, fills the glyphs instead of `color`: Vertical runs over the line's height (ascent to
  // descent), Horizontal over its width, so the ramp sticks to the glyphs however they're moved (GlyphFx).
  LinearGradient fill;
  float outline = 0.0f;  // thickness as a fraction of the text size (~0.05 is bold); 0 = none
  Color outline_color{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec2 shadow_offset{0.0f};  // in Canvas units; zero = no shadow
  Color shadow_color{0.0f, 0.0f, 0.0f, 0.5f};
  TextGlow glow;
  // Turns the line about Text()'s `position` (its alignment point), in radians, clockwise on screen like Arc's
  // angles (a slip pinned askew). The shadow still falls by shadow_offset on screen: the light doesn't turn.
  float rotation = 0.0f;

  // Chunky outlined text with a drop shadow: readable over anything (game HUDs).
  static TextStyle Outlined(const Color& color, const Color& ink = {0.06f, 0.07f, 0.12f, 1.0f}, float alpha = 1.0f) {
    TextStyle style;
    style.color = color.Faded(alpha);
    style.outline = 0.07f;
    style.outline_color = ink.Faded(alpha);
    style.shadow_offset = {0.0f, 3.0f};
    style.shadow_color = {0.0f, 0.0f, 0.0f, 0.45f * alpha};
    return style;
  }
};

// One glyph of a Text() call, handed to a per-glyph effect: it reads where the glyph is and writes how to move and
// tint it (Godot's CharFXTransform, TextMeshPro's per-character vertex animation). The effect runs once per glyph
// per call; the glyph's glow, shadow, outline and fill all follow it.
struct GlyphFx {
  // In:
  int index = 0;           // its character's position in the string
  int order = 0;           // 0..count-1 among the glyphs drawn (spaces have none): for staggers and sweeps
  int count = 0;           // glyphs drawn
  glm::vec2 center{0.0f};  // the middle of the glyph at the line's vertical middle, Canvas units (unturned line)
  // Out (they start at "no change"):
  glm::vec2 offset{0.0f};              // Canvas units, on screen (after the line's rotation)
  float scale = 1.0f;                  // about `center`
  float rotation = 0.0f;               // radians, clockwise on screen, about `center`
  Color tint{1.0f, 1.0f, 1.0f, 1.0f};  // multiplies every layer's colour (alpha fades the glyph)
};
using GlyphEffect = std::function<void(GlyphFx&)>;

// What the UI shader (shaders/ui.frag) draws for a quad: UIVertex::params.x. The values are ui.frag's
// KIND_* defines (a test checks they agree); Shader quads are drawn by their material instead.
enum class QuadKind : int { Text = 1, Rect = 2, Shader = 3, Image = 4, Triangle = 5, Arc = 6 };

// What the GPU gets per corner of a quad (see the UI shader in ui_renderer_gl.cpp).
struct UIVertex {
  glm::vec2 position{0.0f};  // Canvas units
  glm::vec2 uv{0.0f};        // text: atlas coordinate; rect: position relative to the rect centre
  Color color{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 params{0.0f};  // x = kind (QuadKind); text: y = SDF edge, z = Font::DistanceRange, w = softness (half the
                           // fade in SDF values, 0 = anti-aliasing only: glows); rect: y,z = half size, w = radius;
                           // triangle: uv.x, uv.y, y = distances to its 3 edges; arc: y = radius, z = half thickness,
                           // w = half sweep; image: y = 1 if the texture is premultiplied
};

// A run of consecutive quads that can be drawn in one call: same blend mode, and at most one
// texture they sample (a font's atlas or an image's texture; rectangles sample none).
struct UIBatch {
  std::shared_ptr<const Font> font;                // text: this font's atlas (kept alive until drawn)
  std::shared_ptr<rendering::WTexture2D> texture;  // images: this texture (else null)
  size_t first_quad = 0;
  size_t quad_count = 0;
  std::shared_ptr<rendering::ShaderMaterial> material;  // a ShaderRect: one quad, drawn with this material's shader
  rendering::BlendMode blend =
      rendering::BlendMode::Alpha;  // Image: its style's; ShaderRect: its material's; else Alpha
};

// The 2D UI layer, drawn over the 3D scene (Application::OnRenderUI). Immediate mode: draw
// calls made this frame are collected as quads ("SpriteBatch") and rendered in as few GPU draw
// calls as possible - one per font, rectangles join whichever batch is current.
//
// Coordinates are virtual units with (0, 0) top-left and y down. The screen is scaled so that
// the app's reference size (AppConfig::ui.reference_size) fits on screen; the longer side gets
// extra room, so lay out relative to Size() (and the UI's safe area: ui::UIContext), not to fixed
// numbers.
//
// It only draws: widgets, input and screen geometry are the UI layer's (ui_context.h, widgets.h).
class Canvas {
 public:
  glm::vec2 Size() const { return size_; }  // the whole area it covers, in units
  float PixelsPerUnit() const { return pixels_per_unit_; }
  glm::vec2 FromPixels(glm::vec2 pixels) const { return pixels / pixels_per_unit_; }

  // A filled rectangle; corner_radius rounds the corners (anti-aliased edges either way).
  void FillRect(const Rect& rect, const Color& color, float corner_radius = 0.0f);
  // The same filled with a gradient: one quad per span between stops (plus the flat ends), each keeping the whole
  // rect's shape, so rounded corners and edges stay exact.
  void FillRect(const Rect& rect, const LinearGradient& gradient, float corner_radius = 0.0f);
  // A straight line with round ends, at any angle (e.g. over the 3D scene, between projected points).
  void Line(glm::vec2 from, glm::vec2 to, float thickness, const Color& color);
  // A filled circle.
  void Circle(glm::vec2 center, float radius, const Color& color);
  // A filled triangle (any winding), anti-aliased like the rest.
  void Triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Color& color) { Triangle(a, b, c, color, color, color); }
  // One colour per corner, blended across it (e.g. a light ray fading towards its tip).
  void Triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Color& color_a, const Color& color_b,
                const Color& color_c);
  // Part of a ring with round ends: `radius` to the middle of the stroke; angles in radians,
  // 0 = up (12 o'clock), growing clockwise; `sweep` 2*pi (or more) = a whole ring.
  void Arc(glm::vec2 center, float radius, float thickness, float start, float sweep, const Color& color);
  void Ring(glm::vec2 center, float radius, float thickness, const Color& color) {
    Arc(center, radius, thickness, 0.0f, 7.0f, color);
  }
  // A single line of text. `position` is where the alignment edge (left / centre / right)
  // meets the vertical middle of the line, so centring text on a point is just Align::Center.
  // `effect` (optional) moves and tints each glyph (see GlyphFx): letters dropping in one by one, a wave, a shine
  // sweeping across.
  void Text(const Font& font, std::string_view text, glm::vec2 position, float size, Align align = Align::Left,
            const TextStyle& style = {}, const GlyphEffect& effect = {});

  // A rectangle drawn by a custom shader: the ShaderMaterial's Target::Canvas shader (see
  // rendering/shader.h) with its params; vUV goes 0..1 across the rect, vColor is `tint`. It blends
  // as the material says (material->blend; Alpha by default for canvas shaders). Drawn in order
  // with everything else (e.g. first, as a background). Each one is its own draw call.
  // Nothing is drawn if the material has no valid canvas shader.
  void ShaderRect(const Rect& rect, std::shared_ptr<rendering::ShaderMaterial> material,
                  const Color& tint = {1.0f, 1.0f, 1.0f, 1.0f});

  // A texture over a rectangle (a sprite), as `style` says (tint, part of the texture, blending).
  // Texture alpha is straight (not premultiplied), like image files. Consecutive images with the
  // same texture and blend mode are ONE draw call (rectangles join any Alpha run), so thousands of
  // sprites stay cheap. A viewport's texture is upside down here (GL keeps row 0 at the bottom):
  // pass uv = {{0, 1}, {1, 0}}, and premultiplied = true.
  void Image(const Rect& rect, const std::shared_ptr<rendering::WTexture2D>& texture, const ImageStyle& style = {});

  // Multiplies the colour (alpha too) of everything drawn while the returned scope lives: fades and tints a whole
  // group of drawing (Godot's modulate). Scopes nest (their colours multiply); each frame starts at white. Each
  // item is faded on its own, not the group as one picture: overlapping items show through each other. It only
  // changes the look: a faded widget still takes taps.
  //   { auto fade = canvas.Fade(0.5f); DrawCard(...); }
  class ModulateScope {
   public:
    ModulateScope(const ModulateScope&) = delete;
    ModulateScope& operator=(const ModulateScope&) = delete;
    ~ModulateScope() { canvas_.modulate_ = previous_; }

   private:
    friend class Canvas;
    ModulateScope(Canvas& canvas, const Color& color) : canvas_(canvas), previous_(canvas.modulate_) {
      canvas.modulate_ = previous_ * color;
    }
    Canvas& canvas_;
    Color previous_;
  };
  [[nodiscard]] ModulateScope Modulate(const Color& color) { return ModulateScope(*this, color); }
  [[nodiscard]] ModulateScope Fade(float alpha) { return Modulate({1.0f, 1.0f, 1.0f, alpha}); }
  const Color& CurrentModulate() const { return modulate_; }

  const std::vector<UIVertex>& Vertices() const { return vertices_; }  // 4 per quad
  const std::vector<UIBatch>& Batches() const { return batches_; }
  size_t QuadCount() const { return vertices_.size() / 4; }
  glm::mat4 Projection() const;  // units -> clip space

  class InternalAccess;
  // Engine-only parts (the Application's): see InternalAccess below.
  InternalAccess Internal();

 private:
  // Empties it, sized for `framebuffer_pixels` with `reference` units fitting (see above).
  void BeginFrame(glm::vec2 framebuffer_pixels, glm::vec2 reference);

  // The batch for a quad sampling `font`'s atlas, `texture`, or nothing (both null).
  UIBatch& BatchFor(const Font* font, const std::shared_ptr<rendering::WTexture2D>* texture,
                    rendering::BlendMode blend = rendering::BlendMode::Alpha);
  void AddQuad(const Font* font, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv_min,
               const glm::vec2& uv_max, const Color& color, const glm::vec4& params);
  void AddQuad(const Font* font, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], const Color& color,
               const glm::vec4& params);
  // One colour per corner (in the corners' order), before the modulate.
  void AddQuad(const Font* font, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], const Color (&colors)[4],
               const glm::vec4& params, rendering::BlendMode blend = rendering::BlendMode::Alpha);

  // One layer of a Text() call (glow, shadow, outline or fill): a colour or a gradient, where to cut the distance
  // field, how softly, and a screen offset.
  struct TextLayer {
    Color color;
    const LinearGradient* fill = nullptr;
    float edge = 0.5f;
    float softness = 0.0f;
    glm::vec2 offset{0.0f};
    rendering::BlendMode blend = rendering::BlendMode::Alpha;
  };
  // Where Text() put the line: its glyphs (glyph_scratch_) and their effects (fx_scratch_) are laid out already.
  struct TextFrame {
    glm::vec2 pivot{0.0f};       // Text()'s position: the line turns about it
    glm::vec2 turn{1.0f, 0.0f};  // the line's rotation: cos, sin
    Rect box;                    // the unturned line: pen x .. + width, ascent .. descent (for gradients)
    float size = 0.0f;           // Text()'s size: atlas pixels -> units (trimming each layer's quads)
  };
  void AddTextLayer(const Font& font, const TextFrame& frame, const TextLayer& layer);

  glm::vec2 size_{1.0f};
  float pixels_per_unit_ = 1.0f;
  Color modulate_{1.0f, 1.0f, 1.0f, 1.0f};  // see Modulate
  std::vector<UIVertex> vertices_;
  std::vector<UIBatch> batches_;
  std::vector<GlyphQuad> glyph_scratch_;  // reused: no allocation per Text() call
  std::vector<GlyphFx> fx_scratch_;       // one per glyph, when Text() has an effect
};

// For the engine (the Application begins every canvas's frame), not games.
class Canvas::InternalAccess {
 public:
  explicit InternalAccess(Canvas& canvas) : canvas_(canvas) {}
  void BeginFrame(glm::vec2 framebuffer_pixels, glm::vec2 reference) const {
    canvas_.BeginFrame(framebuffer_pixels, reference);
  }

 private:
  Canvas& canvas_;
};

inline Canvas::InternalAccess Canvas::Internal() { return InternalAccess(*this); }

}  // namespace wlw::ui
