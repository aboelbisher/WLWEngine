#include "wlw/ui/canvas.h"

#include <algorithm>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

namespace wlw::ui {

namespace {
constexpr float kKindText = 1.0f;
constexpr float kKindRect = 2.0f;
constexpr float kKindShader = 3.0f;  // drawn by a material's shader, not the UI's
constexpr float kKindImage = 4.0f;
}  // namespace

void Canvas::BeginFrame(glm::vec2 framebuffer_pixels, glm::vec2 reference) {
  // "Fit": the largest scale at which the whole reference area is visible.
  pixels_per_unit_ = std::max(std::min(framebuffer_pixels.x / reference.x, framebuffer_pixels.y / reference.y), 1e-4f);
  size_ = framebuffer_pixels / pixels_per_unit_;
  vertices_.clear();
  batches_.clear();
}

glm::mat4 Canvas::Projection() const {
  return glm::ortho(0.0f, size_.x, size_.y, 0.0f, -1.0f, 1.0f);  // top-left origin, y down
}

void Canvas::AddQuad(const Font* font, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv_min,
                     const glm::vec2& uv_max, const core::Color& color, const glm::vec4& params) {
  const glm::vec2 corners[4] = {{min.x, min.y}, {max.x, min.y}, {max.x, max.y}, {min.x, max.y}};
  const glm::vec2 uvs[4] = {{uv_min.x, uv_min.y}, {uv_max.x, uv_min.y}, {uv_max.x, uv_max.y}, {uv_min.x, uv_max.y}};
  AddQuad(font, corners, uvs, color, params);
}

void Canvas::AddQuad(const Font* font, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], const core::Color& color,
                     const glm::vec4& params) {
  ++BatchFor(font, nullptr).quad_count;
  for (int i = 0; i < 4; ++i) vertices_.push_back({corners[i], uvs[i], color, params});
}

UIBatch& Canvas::BatchFor(const Font* font, const std::shared_ptr<rendering::WTexture2D>* texture,
                          rendering::BlendMode blend) {
  // Batching: keep adding to the current run unless this quad needs another blend mode or samples
  // a different texture than the run does (a run samples at most one: a font atlas or an image).
  // Rectangles sample nothing, so they fit in any run. A ShaderRect's run is its own: whatever
  // comes after it starts a new one. Only pointers are compared: nothing is copied per quad.
  const rendering::WTexture2D* image = texture ? texture->get() : nullptr;
  const bool samples = font != nullptr || image != nullptr;
  if (!batches_.empty()) {
    UIBatch& last = batches_.back();
    const bool last_samples = last.font != nullptr || last.texture != nullptr;
    const bool same_texture = last.font == font && last.texture.get() == image;
    if (!last.material && last.blend == blend && (!samples || !last_samples || same_texture)) {
      if (font) last.font = font;                         // a run of rectangles becomes a text run...
      if (image && !last.texture) last.texture = *texture;  // ...or an image run
      return last;
    }
  }
  UIBatch batch;
  batch.font = font;
  if (image) batch.texture = *texture;
  batch.first_quad = QuadCount();
  batch.blend = blend;
  batches_.push_back(std::move(batch));
  return batches_.back();
}

void Canvas::FillRect(const Rect& rect, const core::Color& color, float corner_radius) {
  // The shader computes the rounded shape from the position relative to the centre (uv) and the
  // half size, so any size and radius is one quad and needs no texture.
  const glm::vec2 half = rect.Size() * 0.5f;
  const float radius = std::clamp(corner_radius, 0.0f, std::min(half.x, half.y));
  AddQuad(nullptr, rect.min, rect.max, -half, half, color, {kKindRect, half.x, half.y, radius});
}

void Canvas::ShaderRect(const Rect& rect, std::shared_ptr<rendering::ShaderMaterial> material, const core::Color& tint) {
  if (!material) return;
  UIBatch batch;
  batch.first_quad = QuadCount();
  batch.quad_count = 1;
  batch.blend = material->blend;
  batch.material = std::move(material);
  batches_.push_back(std::move(batch));
  const glm::vec2 corners[4] = {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}};
  const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  for (int i = 0; i < 4; ++i) vertices_.push_back({corners[i], uvs[i], tint, {kKindShader, 0.0f, 0.0f, 0.0f}});
}

void Canvas::Image(const Rect& rect, const std::shared_ptr<rendering::WTexture2D>& texture, const ImageStyle& style) {
  if (!texture) return;
  ++BatchFor(nullptr, &texture, style.blend).quad_count;
  const Rect& uv = style.uv;
  const core::Color& tint = style.tint;
  const glm::vec2 corners[4] = {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}};
  const glm::vec2 uvs[4] = {uv.min, {uv.max.x, uv.min.y}, uv.max, {uv.min.x, uv.max.y}};
  for (int i = 0; i < 4; ++i) vertices_.push_back({corners[i], uvs[i], tint, {kKindImage, 0.0f, 0.0f, 0.0f}});
}

void Canvas::Line(glm::vec2 from, glm::vec2 to, float thickness, const core::Color& color) {
  // A rounded rectangle turned along the line: its corners are rotated, but the uv still gives
  // positions in the line's own frame, so the shader's rounded-box distance draws a capsule
  // (round ends, anti-aliased) at any angle.
  const glm::vec2 delta = to - from;
  const float length = glm::length(delta);
  if (length < 1e-4f) return;
  const glm::vec2 along = delta / length;
  const glm::vec2 across(-along.y, along.x);
  const glm::vec2 half(length * 0.5f + thickness * 0.5f, thickness * 0.5f);
  const glm::vec2 center = (from + to) * 0.5f;
  const glm::vec2 corners[4] = {center - along * half.x - across * half.y, center + along * half.x - across * half.y,
                                center + along * half.x + across * half.y, center - along * half.x + across * half.y};
  const glm::vec2 uvs[4] = {{-half.x, -half.y}, {half.x, -half.y}, {half.x, half.y}, {-half.x, half.y}};
  AddQuad(nullptr, corners, uvs, color, {kKindRect, half.x, half.y, half.y});
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
