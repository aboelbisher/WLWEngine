#include "wlw/ui/canvas.h"

#include <algorithm>
#include <string>

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
  const glm::vec2 corners[4] = {{min.x, min.y}, {max.x, min.y}, {max.x, max.y}, {min.x, max.y}};
  const glm::vec2 uvs[4] = {{uv_min.x, uv_min.y}, {uv_max.x, uv_min.y}, {uv_max.x, uv_max.y}, {uv_min.x, uv_max.y}};
  AddQuad(font, corners, uvs, color, params);
}

void Canvas::AddQuad(const Font* font, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], const core::Color& color,
                     const glm::vec4& params) {
  // Batching: keep adding to the current run unless this quad needs a different atlas.
  // Rectangles (font == nullptr) don't sample any texture, so they fit in any run.
  if (batches_.empty() || (font && batches_.back().font && batches_.back().font != font)) {
    batches_.push_back({font, QuadCount(), 0});
  }
  UIBatch& batch = batches_.back();
  if (font) batch.font = font;
  ++batch.quad_count;

  for (int i = 0; i < 4; ++i) vertices_.push_back({corners[i], uvs[i], color, params});
}

void Canvas::FillRect(const Rect& rect, const core::Color& color, float corner_radius) {
  // The shader computes the rounded shape from the position relative to the centre (uv) and the
  // half size, so any size and radius is one quad and needs no texture.
  const glm::vec2 half = rect.Size() * 0.5f;
  const float radius = std::clamp(corner_radius, 0.0f, std::min(half.x, half.y));
  AddQuad(nullptr, rect.min, rect.max, -half, half, color, {kKindRect, half.x, half.y, radius});
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

bool Canvas::Button(const Font& font, std::string_view label, const Rect& rect, const ButtonStyle& style) {
  // Immediate mode keeps no widget objects, so a button is recognised from frame to frame by an
  // id made from what identifies it: its label and where it is (FNV-1a hash).
  uint64_t id = 1469598103934665603ull;
  auto mix = [&id](uint64_t value) { id = (id ^ value) * 1099511628211ull; };
  for (char c : label) mix(static_cast<unsigned char>(c));
  mix(static_cast<uint64_t>(static_cast<int64_t>(rect.Center().x * 4.0f)));
  mix(static_cast<uint64_t>(static_cast<int64_t>(rect.Center().y * 4.0f)));
  if (id == 0) id = 1;

  const glm::vec2 p = pointer_.position;
  const bool inside = p.x >= rect.min.x && p.x <= rect.max.x && p.y >= rect.min.y && p.y <= rect.max.y;
  bool clicked = false;
  if (interactive_) {
    if (pointer_.pressed && inside) active_button_ = id;  // went down on this button
    if (active_button_ == id && pointer_.released) {     // ...and up: a click only if still on it
      clicked = inside;
      active_button_ = 0;
    } else if (active_button_ == id && !pointer_.down) {
      active_button_ = 0;  // the press ended elsewhere without us seeing the release
    }
  }
  const bool held = interactive_ && active_button_ == id && pointer_.down && inside;

  // A darker slab under the face; held, the face sinks onto it and shrinks a little.
  const glm::vec2 depth(0.0f, 4.0f);
  if (style.shadow.a > 0.0f) FillRect({rect.min + depth, rect.max + depth}, style.shadow, style.corner_radius);
  Rect face = rect;
  if (held) {
    const glm::vec2 shrink = rect.Size() * 0.03f;
    face = {rect.min + shrink + depth * 0.6f, rect.max - shrink + depth * 0.6f};
  }
  core::Color fill = style.fill;
  if (held) fill = {fill.r * 0.85f, fill.g * 0.85f, fill.b * 0.85f, fill.a};
  FillRect(face, fill, style.corner_radius);
  const float text_size = style.text_size > 0.0f ? style.text_size : rect.Size().y * 0.45f;
  TextStyle text;
  text.color = style.text;
  Text(font, label, face.Center(), text_size, Align::Center, text);
  return clicked;
}


bool Canvas::Toggle(const Font& font, std::string_view label, bool& value, const Rect& rect, const ButtonStyle& style) {
  ButtonStyle look = style;
  if (!value) {  // off: the same button, faded
    look.fill = {style.fill.r * 0.55f, style.fill.g * 0.55f, style.fill.b * 0.55f, style.fill.a};
    look.text = {style.text.r, style.text.g, style.text.b, style.text.a * 0.6f};
  }
  std::string text(label);
  text += value ? " ON" : " OFF";
  if (!Button(font, text, rect, look)) return false;
  value = !value;
  return true;
}

void Canvas::Panel(const Rect& rect, const core::Color& fill, float corner_radius, const core::Color& shadow) {
  if (shadow.a > 0.0f) FillRect({rect.min + glm::vec2(0.0f, 6.0f), rect.max + glm::vec2(0.0f, 6.0f)}, shadow, corner_radius);
  FillRect(rect, fill, corner_radius);
}
}  // namespace wlw::ui
