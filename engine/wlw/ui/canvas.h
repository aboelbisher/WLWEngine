#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/platform/platform.h"
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

// How a ui::Canvas::Button looks.
struct ButtonStyle {
  core::Color fill{0.98f, 0.76f, 0.25f, 1.0f};
  core::Color text{0.10f, 0.08f, 0.16f, 1.0f};
  float text_size = 0.0f;  // 0 = 45% of the button height
  float corner_radius = 14.0f;
  core::Color shadow{0.0f, 0.0f, 0.0f, 0.35f};  // a darker slab under the button; alpha 0 = none
};

// The pointer as UI widgets see it this frame, in Canvas units (Application fills it in).
struct PointerState {
  glm::vec2 position{0.0f};
  bool down = false;      // held
  bool pressed = false;   // went down this frame
  bool released = false;  // went up this frame
};

// How Canvas::Image draws its texture.
struct ImageStyle {
  core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};        // multiplies the texture (white texture + tint = any colour)
  Rect uv{{0.0f, 0.0f}, {1.0f, 1.0f}};             // the part of the texture (sprite sheets); (0, 0) = first row uploaded
  rendering::BlendMode blend = rendering::BlendMode::Alpha;  // e.g. Additive for glows (see blend_mode.h)
};

// How text looks. Outline and shadow come from the same SDF atlas (see Font).
struct TextStyle {
  core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float outline = 0.0f;  // thickness as a fraction of the text size (~0.05 is bold); 0 = none
  core::Color outline_color{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec2 shadow_offset{0.0f};  // in Canvas units; zero = no shadow
  core::Color shadow_color{0.0f, 0.0f, 0.0f, 0.5f};

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

// What the GPU gets per corner of a quad (see the UI shader in ui_renderer_gl.cpp).
struct UIVertex {
  glm::vec2 position{0.0f};  // Canvas units
  glm::vec2 uv{0.0f};        // text: atlas coordinate; rect: position relative to the rect centre
  core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 params{0.0f};  // x = kind (1 text, 2 rect, 3 shader, 4 image); text: y = SDF edge; rect: y,z = half size, w = radius
};

// A run of consecutive quads that can be drawn in one call: same blend mode, and at most one
// texture they sample (a font's atlas or an image's texture; rectangles sample none).
struct UIBatch {
  const Font* font = nullptr;                          // text: this font's atlas
  std::shared_ptr<rendering::WTexture2D> texture;      // images: this texture (else null)
  size_t first_quad = 0;
  size_t quad_count = 0;
  std::shared_ptr<rendering::ShaderMaterial> material;  // a ShaderRect: one quad, drawn with this material's shader
  rendering::BlendMode blend = rendering::BlendMode::Alpha;  // Image: its style's; ShaderRect: its material's; else Alpha
};

// The 2D UI layer, drawn over the 3D scene (Application::OnRenderUI). Immediate mode: draw
// calls made this frame are collected as quads ("SpriteBatch") and rendered in as few GPU draw
// calls as possible - one per font, rectangles join whichever batch is current.
//
// Coordinates are virtual units with (0, 0) top-left and y down. The screen is scaled so that
// the app's reference size (AppConfig::ui_reference_size) fits on screen; the longer side gets
// extra room, so lay out relative to Size() and SafeArea(), not to fixed numbers.
class Canvas {
 public:
  void BeginFrame(glm::vec2 framebuffer_pixels, const platform::SafeAreaInsets& insets_pixels, glm::vec2 reference);

  glm::vec2 Size() const { return size_; }       // the whole screen, in units
  Rect SafeArea() const { return safe_area_; }   // the part not covered by bars/cutouts
  float PixelsPerUnit() const { return pixels_per_unit_; }
  glm::vec2 FromPixels(glm::vec2 pixels) const { return pixels / pixels_per_unit_; }

  // A filled rectangle; corner_radius rounds the corners (anti-aliased edges either way).
  void FillRect(const Rect& rect, const core::Color& color, float corner_radius = 0.0f);
  // A straight line with round ends, at any angle (e.g. over the 3D scene, between projected points).
  void Line(glm::vec2 from, glm::vec2 to, float thickness, const core::Color& color);
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

  // ---- Widgets ----
  // An immediate-mode button: call it every frame you want it shown; it returns true on the
  // frame it was clicked (pressed on it and released on it - sliding off cancels). It shrinks
  // a little while held. Only responds while the canvas is interactive (see SetInteractive).
  bool Button(const Font& font, std::string_view label, const Rect& rect, const ButtonStyle& style = {});
  // An on/off switch drawn as a button reading "<label> ON" or "<label> OFF" (dimmed when off).
  // Flips `value` and returns true on the frame it was clicked.
  bool Toggle(const Font& font, std::string_view label, bool& value, const Rect& rect, const ButtonStyle& style = {});
  // A rounded box with a soft shadow under it: the background of dialogs and panels.
  void Panel(const Rect& rect, const core::Color& fill, float corner_radius = 24.0f,
             const core::Color& shadow = {0.0f, 0.0f, 0.0f, 0.35f});

  void SetPointer(const PointerState& pointer) { pointer_ = pointer; }
  const PointerState& Pointer() const { return pointer_; }
  // ScreenStack turns this off while drawing screens that aren't on top: their buttons are
  // visible but can't be clicked.
  void SetInteractive(bool interactive) { interactive_ = interactive; }
  bool Interactive() const { return interactive_; }

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
  void AddText(const Font& font, std::string_view text, glm::vec2 pen, float size, const core::Color& color, float edge);

  glm::vec2 size_{1.0f};
  Rect safe_area_;
  float pixels_per_unit_ = 1.0f;
  std::vector<UIVertex> vertices_;
  std::vector<UIBatch> batches_;
  std::vector<GlyphQuad> glyph_scratch_;  // reused: no allocation per Text() call
  PointerState pointer_;
  bool interactive_ = true;
  uint64_t active_button_ = 0;  // the button the pointer went down on (0 = none); kept across frames
};

}  // namespace wlw::ui
