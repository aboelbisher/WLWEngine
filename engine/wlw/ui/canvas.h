#pragma once

#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/core/color.h"
#include "wlw/platform/platform.h"
#include "wlw/ui/font.h"

namespace wlw::ui {

enum class Align { Left, Center, Right };

struct Rect {
  glm::vec2 min{0.0f};
  glm::vec2 max{0.0f};
  glm::vec2 Center() const { return (min + max) * 0.5f; }
  glm::vec2 Size() const { return max - min; }
};

// How text looks. Outline and shadow come from the same SDF atlas (see Font).
struct TextStyle {
  core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float outline = 0.0f;  // thickness as a fraction of the text size (~0.05 is bold); 0 = none
  core::Color outline_color{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec2 shadow_offset{0.0f};  // in Canvas units; zero = no shadow
  core::Color shadow_color{0.0f, 0.0f, 0.0f, 0.5f};
};

// What the GPU gets per corner of a quad (see the UI shader in ui_renderer_gl.cpp).
struct UIVertex {
  glm::vec2 position{0.0f};  // Canvas units
  glm::vec2 uv{0.0f};        // text: atlas coordinate; rect: position relative to the rect centre
  core::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 params{0.0f};  // x = kind (1 text, 2 rect); text: y = SDF edge; rect: y,z = half size, w = radius
};

// A run of consecutive quads that can be drawn in one call (same font atlas, or none).
struct UIBatch {
  const Font* font = nullptr;
  size_t first_quad = 0;
  size_t quad_count = 0;
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
  // A single line of text. `position` is where the alignment edge (left / centre / right)
  // meets the vertical middle of the line, so centring text on a point is just Align::Center.
  void Text(const Font& font, std::string_view text, glm::vec2 position, float size, Align align = Align::Left,
            const TextStyle& style = {});

  const std::vector<UIVertex>& Vertices() const { return vertices_; }  // 4 per quad
  const std::vector<UIBatch>& Batches() const { return batches_; }
  size_t QuadCount() const { return vertices_.size() / 4; }
  glm::mat4 Projection() const;  // units -> clip space

 private:
  void AddQuad(const Font* font, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv_min,
               const glm::vec2& uv_max, const core::Color& color, const glm::vec4& params);
  void AddText(const Font& font, std::string_view text, glm::vec2 pen, float size, const core::Color& color, float edge);

  glm::vec2 size_{1.0f};
  Rect safe_area_;
  float pixels_per_unit_ = 1.0f;
  std::vector<UIVertex> vertices_;
  std::vector<UIBatch> batches_;
  std::vector<GlyphQuad> glyph_scratch_;  // reused: no allocation per Text() call
};

}  // namespace wlw::ui
