#include "wlw/ui/canvas.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace wlw::ui {

namespace {
constexpr float kKindText = 1.0f;
constexpr float kKindRect = 2.0f;
}  // namespace

void Canvas::BeginFrame(glm::vec2 framebuffer_pixels, const platform::SafeAreaInsets& insets, glm::vec2 reference) {
  // "Fit": the largest scale at which the whole reference area is visible.
  pixels_per_unit_ = std::max(std::min(framebuffer_pixels.x / reference.x, framebuffer_pixels.y / reference.y), 1e-4f);
  size_ = framebuffer_pixels / pixels_per_unit_;
  safe_area_.min = glm::vec2(insets.left, insets.top) / pixels_per_unit_;
  safe_area_.max = size_ - glm::vec2(insets.right, insets.bottom) / pixels_per_unit_;
  vertices_.clear();
  batches_.clear();
}

glm::mat4 Canvas::Projection() const {
  return glm::ortho(0.0f, size_.x, size_.y, 0.0f, -1.0f, 1.0f);  // top-left origin, y down
}

void Canvas::AddQuad(const Font* font, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv_min,
                     const glm::vec2& uv_max, const core::Color& color, const glm::vec4& params) {
  // Batching: keep adding to the current run unless this quad needs a different atlas.
  // Rectangles (font == nullptr) don't sample any texture, so they fit in any run.
  if (batches_.empty() || (font && batches_.back().font && batches_.back().font != font)) {
    batches_.push_back({font, QuadCount(), 0});
  }
  UIBatch& batch = batches_.back();
  if (font) batch.font = font;
  ++batch.quad_count;

  vertices_.push_back({{min.x, min.y}, {uv_min.x, uv_min.y}, color, params});
  vertices_.push_back({{max.x, min.y}, {uv_max.x, uv_min.y}, color, params});
  vertices_.push_back({{max.x, max.y}, {uv_max.x, uv_max.y}, color, params});
  vertices_.push_back({{min.x, max.y}, {uv_min.x, uv_max.y}, color, params});
}

void Canvas::FillRect(const Rect& rect, const core::Color& color, float corner_radius) {
  // The shader computes the rounded shape from the position relative to the centre (uv) and the
  // half size, so any size and radius is one quad and needs no texture.
  const glm::vec2 half = rect.Size() * 0.5f;
  const float radius = std::clamp(corner_radius, 0.0f, std::min(half.x, half.y));
  AddQuad(nullptr, rect.min, rect.max, -half, half, color, {kKindRect, half.x, half.y, radius});
}

void Canvas::AddText(const Font& font, std::string_view text, glm::vec2 pen, float size, const core::Color& color,
                     float edge) {
  glyph_scratch_.clear();
  font.Layout(text, size, pen, glyph_scratch_);
  for (const GlyphQuad& q : glyph_scratch_) {
    AddQuad(&font, q.min, q.max, q.uv_min, q.uv_max, color, {kKindText, edge, 0.0f, 0.0f});
  }
}

void Canvas::Text(const Font& font, std::string_view text, glm::vec2 position, float size, Align align,
                  const TextStyle& style) {
  const float width = font.Measure(text, size);
  glm::vec2 pen = position;
  if (align == Align::Center) pen.x -= width * 0.5f;
  if (align == Align::Right) pen.x -= width;
  // From the middle of the line box (ascent..descent) down to the baseline.
  pen.y += (font.Ascent(size) - font.Descent(size)) * 0.5f;

  // Back to front: shadow, outline, then the fill on top.
  const float outline_edge = font.EdgeForOutline(style.outline);
  if (style.shadow_offset != glm::vec2(0.0f)) {
    AddText(font, text, pen + style.shadow_offset, size, style.shadow_color, style.outline > 0.0f ? outline_edge : 0.5f);
  }
  if (style.outline > 0.0f) AddText(font, text, pen, size, style.outline_color, outline_edge);
  AddText(font, text, pen, size, style.color, 0.5f);
}

}  // namespace wlw::ui
