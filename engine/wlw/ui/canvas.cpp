#include "wlw/ui/canvas.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

namespace wlw::ui {

namespace {
constexpr float kKindText = static_cast<float>(QuadKind::Text);
constexpr float kKindRect = static_cast<float>(QuadKind::Rect);
constexpr float kKindShader = static_cast<float>(QuadKind::Shader);  // drawn by a material's shader, not the UI's
constexpr float kKindImage = static_cast<float>(QuadKind::Image);
constexpr float kKindTriangle = static_cast<float>(QuadKind::Triangle);
constexpr float kKindArc = static_cast<float>(QuadKind::Arc);
}  // namespace

void Canvas::BeginFrame(glm::vec2 framebuffer_pixels, glm::vec2 reference) {
  // "Fit": the largest scale at which the whole reference area is visible.
  pixels_per_unit_ = std::max(std::min(framebuffer_pixels.x / reference.x, framebuffer_pixels.y / reference.y), 1e-4f);
  size_ = framebuffer_pixels / pixels_per_unit_;
  modulate_ = {1.0f, 1.0f, 1.0f, 1.0f};
  vertices_.clear();
  batches_.clear();
}

glm::mat4 Canvas::Projection() const {
  return glm::ortho(0.0f, size_.x, size_.y, 0.0f, -1.0f, 1.0f);  // top-left origin, y down
}

void Canvas::AddQuad(const Font* font, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv_min,
                     const glm::vec2& uv_max, const Color& color, const glm::vec4& params) {
  const glm::vec2 corners[4] = {{min.x, min.y}, {max.x, min.y}, {max.x, max.y}, {min.x, max.y}};
  const glm::vec2 uvs[4] = {{uv_min.x, uv_min.y}, {uv_max.x, uv_min.y}, {uv_max.x, uv_max.y}, {uv_min.x, uv_max.y}};
  AddQuad(font, corners, uvs, color, params);
}

void Canvas::AddQuad(const Font* font, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], const Color& color,
                     const glm::vec4& params) {
  const Color colors[4] = {color, color, color, color};
  AddQuad(font, corners, uvs, colors, params);
}

void Canvas::AddQuad(const Font* font, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4],
                     const Color (&colors)[4], const glm::vec4& params) {
  ++BatchFor(font, nullptr).quad_count;
  for (int i = 0; i < 4; ++i) vertices_.push_back({corners[i], uvs[i], colors[i] * modulate_, params});
}

namespace {

// What a batch holds its font by: a shared reference when the font is owned by a shared_ptr (the caller may
// drop its font before the draw), else a plain pointer (a uniquely owned font must outlive the draw).
std::shared_ptr<const Font> KeepFont(const Font& font) {
  if (std::shared_ptr<const Font> shared = font.weak_from_this().lock()) return shared;
  return std::shared_ptr<const Font>(std::shared_ptr<const Font>(), &font);  // aliasing, owns nothing
}

}  // namespace

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
    const bool same_texture = last.font.get() == font && last.texture.get() == image;
    if (!last.material && last.blend == blend && (!samples || !last_samples || same_texture)) {
      if (font && !last.font) last.font = KeepFont(*font);  // a run of rectangles becomes a text run...
      if (image && !last.texture) last.texture = *texture;  // ...or an image run
      return last;
    }
  }
  UIBatch batch;
  if (font) batch.font = KeepFont(*font);
  if (image) batch.texture = *texture;
  batch.first_quad = QuadCount();
  batch.blend = blend;
  batches_.push_back(std::move(batch));
  return batches_.back();
}

void Canvas::FillRect(const Rect& rect, const Color& color, float corner_radius) {
  // The shader computes the rounded shape from the position relative to the centre (uv) and the
  // half size, so any size and radius is one quad and needs no texture.
  const glm::vec2 half = rect.Size() * 0.5f;
  const float radius = std::clamp(corner_radius, 0.0f, std::min(half.x, half.y));
  AddQuad(nullptr, rect.min, rect.max, -half, half, color, {kKindRect, half.x, half.y, radius});
}

void Canvas::FillRect(const Rect& rect, const LinearGradient& gradient, float corner_radius) {
  const size_t count = std::min(gradient.count, LinearGradient::kMaxStops);
  if (count == 0) return;
  if (count == 1) return FillRect(rect, gradient.stops[0].color, corner_radius);
  // Split along the gradient into spans: a flat one before the first stop, one between each pair of stops, a flat
  // one after the last. Every span is a quad whose uv still measures from the WHOLE rect's centre with the whole
  // half size, so the shader draws the same rounded rect, cut into strips that meet exactly. Each strip's colour
  // varies along one axis only, so its two triangles agree (no diagonal seam).
  const glm::vec2 half = rect.Size() * 0.5f;
  const glm::vec2 center = rect.Center();
  const float radius = std::clamp(corner_radius, 0.0f, std::min(half.x, half.y));
  const glm::vec4 params{kKindRect, half.x, half.y, radius};
  const bool vertical = gradient.direction == LinearGradient::Direction::Vertical;
  const auto span = [&](float t0, float t1, const Color& c0, const Color& c1) {
    t0 = std::clamp(t0, 0.0f, 1.0f);
    t1 = std::clamp(t1, 0.0f, 1.0f);
    if (t1 <= t0) return;
    glm::vec2 min = rect.min, max = rect.max;
    if (vertical) {
      min.y = rect.min.y + rect.Size().y * t0;
      max.y = rect.min.y + rect.Size().y * t1;
    } else {
      min.x = rect.min.x + rect.Size().x * t0;
      max.x = rect.min.x + rect.Size().x * t1;
    }
    const glm::vec2 corners[4] = {{min.x, min.y}, {max.x, min.y}, {max.x, max.y}, {min.x, max.y}};
    const glm::vec2 uvs[4] = {corners[0] - center, corners[1] - center, corners[2] - center, corners[3] - center};
    // Corner order: top left, top right, bottom right, bottom left.
    const Color colors[4] = {c0, vertical ? c0 : c1, c1, vertical ? c1 : c0};
    AddQuad(nullptr, corners, uvs, colors, params);
  };
  const auto& stops = gradient.stops;
  span(0.0f, stops[0].at, stops[0].color, stops[0].color);
  for (size_t i = 0; i + 1 < count; ++i) span(stops[i].at, stops[i + 1].at, stops[i].color, stops[i + 1].color);
  span(stops[count - 1].at, 1.0f, stops[count - 1].color, stops[count - 1].color);
}

void Canvas::ShaderRect(const Rect& rect, std::shared_ptr<rendering::ShaderMaterial> material, const Color& tint) {
  if (!material) return;
  UIBatch batch;
  batch.first_quad = QuadCount();
  batch.quad_count = 1;
  batch.blend = material->blend;
  batch.material = std::move(material);
  batches_.push_back(std::move(batch));
  const glm::vec2 corners[4] = {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}};
  const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  for (int i = 0; i < 4; ++i)
    vertices_.push_back({corners[i], uvs[i], tint * modulate_, {kKindShader, 0.0f, 0.0f, 0.0f}});
}

void Canvas::Image(const Rect& rect, const std::shared_ptr<rendering::WTexture2D>& texture, const ImageStyle& style) {
  if (!texture) return;
  ++BatchFor(nullptr, &texture, style.blend).quad_count;
  const Rect& uv = style.uv;
  const Color tint = style.tint * modulate_;
  const glm::vec2 corners[4] = {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}};
  const glm::vec2 uvs[4] = {uv.min, {uv.max.x, uv.min.y}, uv.max, {uv.min.x, uv.max.y}};
  const glm::vec4 params{kKindImage, style.premultiplied ? 1.0f : 0.0f, 0.0f, 0.0f};
  for (int i = 0; i < 4; ++i) vertices_.push_back({corners[i], uvs[i], tint, params});
}

void Canvas::Line(glm::vec2 from, glm::vec2 to, float thickness, const Color& color) {
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

void Canvas::Circle(glm::vec2 center, float radius, const Color& color) {
  FillRect({center - glm::vec2(radius), center + glm::vec2(radius)}, color, radius);
}

void Canvas::Triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Color& color) {
  const auto cross = [](glm::vec2 u, glm::vec2 v) { return u.x * v.y - u.y * v.x; };
  const float area2 = cross(b - a, c - a);
  if (std::fabs(area2) < 1e-6f) return;
  // The distance to each edge's line is linear across the triangle, so the GPU can interpolate it
  // exactly: each corner carries its distance to the three edges (positive inside), and the shader
  // fades out where the smallest one reaches zero. For room to fade, the triangle grows by about a
  // pixel: each corner moves out along both its edges' normals (a miter).
  const float side = area2 > 0.0f ? 1.0f : -1.0f;
  const auto inward = [&](glm::vec2 from, glm::vec2 to) {
    const glm::vec2 d = glm::normalize(to - from);
    return glm::vec2(-d.y, d.x) * side;
  };
  const glm::vec2 n_ab = inward(a, b), n_bc = inward(b, c), n_ca = inward(c, a);
  const float grow = 1.5f / pixels_per_unit_;
  const auto out = [grow](glm::vec2 p, glm::vec2 n1, glm::vec2 n2) {
    return p - grow * (n1 + n2) / std::max(1.0f + glm::dot(n1, n2), 0.05f);
  };
  const glm::vec2 corners[4] = {out(a, n_ab, n_ca), out(b, n_ab, n_bc), out(c, n_bc, n_ca), out(c, n_bc, n_ca)};
  // One quad whose 4th corner repeats the 3rd: its second half (0-2-3) is empty.
  ++BatchFor(nullptr, nullptr).quad_count;
  for (const glm::vec2& p : corners) {
    const glm::vec2 uv(glm::dot(p - b, n_bc), glm::dot(p - c, n_ca));
    vertices_.push_back({p, uv, color * modulate_, {kKindTriangle, glm::dot(p - a, n_ab), 0.0f, 0.0f}});
  }
}

void Canvas::Arc(glm::vec2 center, float radius, float thickness, float start, float sweep, const Color& color) {
  if (radius <= 0.0f || thickness <= 0.0f || sweep <= 0.0f) return;
  // A square around the circle, turned so its own "down" (+y in uv) points at the middle of the
  // arc: the shader then only needs the half sweep, measured both ways from that axis.
  const float half_sweep = std::min(sweep * 0.5f, 3.14159265f);
  const float middle = start + sweep * 0.5f;
  const glm::vec2 axis(std::sin(middle), -std::cos(middle));  // 0 = up, clockwise (y is down)
  const glm::vec2 across(-axis.y, axis.x);
  const float h = radius + thickness * 0.5f + 1.5f / pixels_per_unit_;
  const glm::vec2 local[4] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
  glm::vec2 corners[4];
  for (int i = 0; i < 4; ++i) corners[i] = center + across * local[i].x + axis * local[i].y;
  AddQuad(nullptr, corners, local, color, {kKindArc, radius, thickness * 0.5f, half_sweep});
}

void Canvas::AddText(const Font& font, std::string_view text, glm::vec2 pen, float size, const Color& color, float edge,
                     glm::vec2 pivot, glm::vec2 turn, glm::vec2 offset) {
  glyph_scratch_.clear();
  font.Layout(text, size, pen, glyph_scratch_);
  const glm::vec4 params{kKindText, edge, font.DistanceRange(), 0.0f};
  const bool turned = turn != glm::vec2(1.0f, 0.0f);
  for (const GlyphQuad& q : glyph_scratch_) {
    if (!turned) {
      AddQuad(&font, q.min + offset, q.max + offset, q.uv_min, q.uv_max, color, params);
      continue;
    }
    // Only the corners turn: the atlas uvs ride along, and the shader's SDF anti-aliasing works from the uv's
    // screen derivatives, so it holds at any angle.
    const glm::vec2 local[4] = {q.min, {q.max.x, q.min.y}, q.max, {q.min.x, q.max.y}};
    const glm::vec2 uvs[4] = {q.uv_min, {q.uv_max.x, q.uv_min.y}, q.uv_max, {q.uv_min.x, q.uv_max.y}};
    glm::vec2 corners[4];
    for (int i = 0; i < 4; ++i) {
      const glm::vec2 d = local[i] - pivot;
      corners[i] = pivot + glm::vec2(d.x * turn.x - d.y * turn.y, d.x * turn.y + d.y * turn.x) + offset;
    }
    AddQuad(&font, corners, uvs, color, params);
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
  // Clockwise on screen (y down): x' = x cos - y sin, y' = x sin + y cos.
  const glm::vec2 turn =
      style.rotation == 0.0f ? glm::vec2(1.0f, 0.0f) : glm::vec2(std::cos(style.rotation), std::sin(style.rotation));

  // Back to front: shadow, outline, then the fill on top.
  const float outline_edge = font.EdgeForOutline(style.outline);
  if (style.shadow_offset != glm::vec2(0.0f)) {
    AddText(font, text, pen, size, style.shadow_color, style.outline > 0.0f ? outline_edge : 0.5f, position, turn,
            style.shadow_offset);
  }
  if (style.outline > 0.0f) AddText(font, text, pen, size, style.outline_color, outline_edge, position, turn);
  AddText(font, text, pen, size, style.color, 0.5f, position, turn);
}

}  // namespace wlw::ui
