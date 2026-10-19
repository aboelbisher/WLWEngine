#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/rendering/blend_mode.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/font.h"

namespace wlw::ui {

enum class Align { Left, Center, Right };

struct Rect {
  glm::vec2 min{0.0f};
  glm::vec2 max{0.0f};
  glm::vec2 Center() const { return (min + max) * 0.5f; }
  glm::vec2 Size() const { return max - min; }
};

// How Canvas::Image draws its texture.
struct ImageStyle {
  core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};  // multiplies the texture (white texture + tint = any colour)
  Rect uv{{0.0f, 0.0f}, {1.0f, 1.0f}};       // the part of the texture (sprite sheets); (0, 0) = first row uploaded
  rendering::BlendMode blend = rendering::BlendMode::Alpha;  // e.g. Additive for glows (see blend_mode.h)
};

// How text looks. Outline and shadow come from the same SDF atlas (see Font).
struct TextStyle {
  core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float outline = 0.0f;  // thickness as a fraction of the text size (~0.05 is bold); 0 = none
  core::Color outline_color{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec2 shadow_offset{0.0f};  // in Canvas units; zero = no shadow
  core::Color shadow_color{0.0f, 0.0f, 0.0f, 0.5f};
  // Turns the line about Text()'s `position` (its alignment point), in radians, clockwise on screen like Arc's
  // angles (a slip pinned askew). The shadow still falls by shadow_offset on screen: the light doesn't turn.
  float rotation = 0.0f;

  // Chunky outlined text with a drop shadow: readable over anything (game HUDs).
  static TextStyle Outlined(const core::Color& color, const core::Color& ink = {0.06f, 0.07f, 0.12f, 1.0f},
                            float alpha = 1.0f) {
    TextStyle style;
    style.color = {color.r, color.g, color.b, color.a * alpha};
    style.outline = 0.07f;
    style.outline_color = {ink.r, ink.g, ink.b, ink.a * alpha};
    style.shadow_offset = {0.0f, 3.0f};
    style.shadow_color = {0.0f, 0.0f, 0.0f, 0.45f * alpha};
    return style;
  }
};

// What the UI shader (shaders/ui.frag) draws for a quad: UIVertex::params.x. The values are ui.frag's
// KIND_* defines (a test checks they agree); Shader quads are drawn by their material instead.
enum class QuadKind : int { Text = 1, Rect = 2, Shader = 3, Image = 4, Triangle = 5, Arc = 6 };

// What the GPU gets per corner of a quad (see the UI shader in ui_renderer_gl.cpp).
struct UIVertex {
  glm::vec2 position{0.0f};  // Canvas units
  glm::vec2 uv{0.0f};        // text: atlas coordinate; rect: position relative to the rect centre
  core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 params{
      0.0f};  // x = kind (QuadKind); text: y = SDF edge, z = Font::DistanceRange; rect: y,z = half size, w = radius;
              // triangle: uv.x, uv.y, y = distances to its 3 edges; arc: y = radius, z = half thickness, w = half sweep
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
// the app's reference size (AppConfig::ui_reference_size) fits on screen; the longer side gets
// extra room, so lay out relative to Size() (and the UI's safe area: ui::UIContext), not to fixed
// numbers.
//
// It only draws: widgets, input and screen geometry are the UI layer's (ui_context.h, widgets.h).
class Canvas {
 public:
  // Empties it, sized for `framebuffer_pixels` with `reference` units fitting (see above).
  void BeginFrame(glm::vec2 framebuffer_pixels, glm::vec2 reference);

  glm::vec2 Size() const { return size_; }  // the whole area it covers, in units
  float PixelsPerUnit() const { return pixels_per_unit_; }
  glm::vec2 FromPixels(glm::vec2 pixels) const { return pixels / pixels_per_unit_; }

  // A filled rectangle; corner_radius rounds the corners (anti-aliased edges either way).
  void FillRect(const Rect& rect, const core::Color& color, float corner_radius = 0.0f);
  // A straight line with round ends, at any angle (e.g. over the 3D scene, between projected points).
  void Line(glm::vec2 from, glm::vec2 to, float thickness, const core::Color& color);
  // A filled circle.
  void Circle(glm::vec2 center, float radius, const core::Color& color);
  // A filled triangle (any winding), anti-aliased like the rest.
  void Triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const core::Color& color);
  // Part of a ring with round ends: `radius` to the middle of the stroke; angles in radians,
  // 0 = up (12 o'clock), growing clockwise; `sweep` 2*pi (or more) = a whole ring.
  void Arc(glm::vec2 center, float radius, float thickness, float start, float sweep, const core::Color& color);
  void Ring(glm::vec2 center, float radius, float thickness, const core::Color& color) {
    Arc(center, radius, thickness, 0.0f, 7.0f, color);
  }
  // A single line of text. `position` is where the alignment edge (left / centre / right)
  // meets the vertical middle of the line, so centring text on a point is just Align::Center.
  void Text(const Font& font, std::string_view text, glm::vec2 position, float size, Align align = Align::Left,
            const TextStyle& style = {});

  // A rectangle drawn by a custom shader: the ShaderMaterial's Target::Canvas shader (see
  // rendering/shader.h) with its params; vUV goes 0..1 across the rect, vColor is `tint`. It blends
  // as the material says (material->blend; Alpha by default for canvas shaders). Drawn in order
  // with everything else (e.g. first, as a background). Each one is its own draw call.
  // Nothing is drawn if the material has no valid canvas shader.
  void ShaderRect(const Rect& rect, std::shared_ptr<rendering::ShaderMaterial> material,
                  const core::Color& tint = {1.0f, 1.0f, 1.0f, 1.0f});

  // A texture over a rectangle (a sprite), as `style` says (tint, part of the texture, blending).
  // Texture alpha is straight (not premultiplied), like image files. Consecutive images with the
  // same texture and blend mode are ONE draw call (rectangles join any Alpha run), so thousands of
  // sprites stay cheap. A viewport's texture is upside down here (GL keeps row 0 at the bottom):
  // pass uv = {{0, 1}, {1, 0}}.
  void Image(const Rect& rect, const std::shared_ptr<rendering::WTexture2D>& texture, const ImageStyle& style = {});

  const std::vector<UIVertex>& Vertices() const { return vertices_; }  // 4 per quad
  const std::vector<UIBatch>& Batches() const { return batches_; }
  size_t QuadCount() const { return vertices_.size() / 4; }
  glm::mat4 Projection() const;  // units -> clip space

 private:
  // The batch for a quad sampling `font`'s atlas, `texture`, or nothing (both null).
  UIBatch& BatchFor(const Font* font, const std::shared_ptr<rendering::WTexture2D>* texture,
                    rendering::BlendMode blend = rendering::BlendMode::Alpha);
  void AddQuad(const Font* font, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv_min,
               const glm::vec2& uv_max, const core::Color& color, const glm::vec4& params);
  void AddQuad(const Font* font, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], const core::Color& color,
               const glm::vec4& params);
  // `pen` = the unturned baseline start; the glyphs are turned by `turn` (cos, sin) about `pivot`, then moved by
  // `offset`.
  void AddText(const Font& font, std::string_view text, glm::vec2 pen, float size, const core::Color& color, float edge,
               glm::vec2 pivot = {}, glm::vec2 turn = {1.0f, 0.0f}, glm::vec2 offset = {});

  glm::vec2 size_{1.0f};
  float pixels_per_unit_ = 1.0f;
  std::vector<UIVertex> vertices_;
  std::vector<UIBatch> batches_;
  std::vector<GlyphQuad> glyph_scratch_;  // reused: no allocation per Text() call
};

}  // namespace wlw::ui
