#include "wlw/debug/debug_draw.h"

// Empty without WLW_DEBUG_DRAW: release builds carry none of this (the header's calls are empty then).
#if WLW_DEBUG_DRAW

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "wlw/core/log.h"

namespace wlw::debug {

WLW_LOG_CATEGORY(LogDebugDraw);

namespace {

constexpr int kCircleSegments = 32;

// Draw-list versions, unique across every DebugDraw (the renderer tells uploads apart by them; the Studio draws two
// apps). Game thread only.
uint64_t g_last_version = 0;

// Where triangles are lit from (world, Z up: above, a little to the front and right). Both sides of a triangle shade
// alike: it's the angle to this direction that shows the shape, not which side faces it.
const Vec3 kShadeDirection = glm::normalize(Vec3(0.35f, -0.5f, 0.8f));

Vec3 Transformed(const Mat4& transform, const Vec3& point) { return Vec3(transform * Vec4(point, 1.0f)); }

// Two unit vectors at right angles to `normal` and to each other (a circle's plane).
void PlaneAxes(const Vec3& normal, Vec3& u, Vec3& v) {
  const Vec3 helper = std::abs(normal.z) < 0.9f ? kUp : kRight;
  u = glm::normalize(glm::cross(normal, helper));
  v = glm::cross(normal, u);
}

}  // namespace

DebugDraw::Meta DebugDraw::MetaFor(const DrawStyle& style) const {
  Meta meta;
  meta.depth = style.depth;
  if (style.seconds > 0.0f) {
    meta.lifetime = Lifetime::Timed;
    meta.expires = now_ + style.seconds;
  } else if (in_fixed_step_) {
    meta.lifetime = Lifetime::FixedStep;
  } else {
    meta.lifetime = moving_ ? Lifetime::Frame : Lifetime::PausedFrame;
  }
  return meta;
}

bool DebugDraw::RoomFor(size_t stored, int max, int adding) {
  if (stored + static_cast<size_t>(adding) <= static_cast<size_t>(max)) return true;
  ++dropped_;
  if (!warned_) {
    warned_ = true;
    WLW_LOG(LogDebugDraw, Warning, "Debug draw is full (%d lines, %d triangles, %d labels): shapes are dropped",
            kMaxLines, kMaxTriangles, kMaxTexts);
  }
  return false;
}

void DebugDraw::AddLine(const Vec3& a, const Vec3& b, const DrawStyle& style) {
  if (!RoomFor(lines_.size(), kMaxLines, 1)) return;
  lines_.push_back({{a, b, Color32(style.color).Packed(), style.width}, MetaFor(style)});
  changed_ = true;
}

void DebugDraw::AddPolyline(std::span<const Vec3> points, bool closed, const DrawStyle& style) {
  if (points.size() < 2) return;
  const size_t count = closed && points.size() > 2 ? points.size() : points.size() - 1;
  if (!RoomFor(lines_.size(), kMaxLines, static_cast<int>(count))) return;
  const Meta meta = MetaFor(style);
  const uint32_t color = Color32(style.color).Packed();
  for (size_t i = 0; i < count; ++i) {
    lines_.push_back({{points[i], points[(i + 1) % points.size()], color, style.width}, meta});
  }
  changed_ = true;
}

void DebugDraw::AddArrow(const Vec3& from, const Vec3& to, const DrawStyle& style, float head) {
  const Vec3 along = to - from;
  const float length = glm::length(along);
  if (length <= 1e-6f) {
    AddLine(from, to, style);
    return;
  }
  if (!RoomFor(lines_.size(), kMaxLines, 5)) return;
  const Vec3 direction = along / length;
  const float head_length = std::min(0.25f * length, std::max(head, 0.0f));
  const float head_radius = 0.4f * head_length;
  Vec3 u, v;
  PlaneAxes(direction, u, v);
  const Vec3 base = to - direction * head_length;
  const Meta meta = MetaFor(style);
  const uint32_t color = Color32(style.color).Packed();
  lines_.push_back({{from, to, color, style.width}, meta});
  for (const Vec3& side : {u, -u, v, -v}) lines_.push_back({{to, base + side * head_radius, color, style.width}, meta});
  changed_ = true;
}

void DebugDraw::AddBox(const Mat4& transform, const Vec3& center, const Vec3& half_extents, const DrawStyle& style) {
  if (!RoomFor(lines_.size(), kMaxLines, 12)) return;
  // Corner i: bit 0 = +x, bit 1 = +y, bit 2 = +z.
  Vec3 corners[8];
  for (int i = 0; i < 8; ++i) {
    const Vec3 sign((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
    corners[i] = Transformed(transform, center + sign * half_extents);
  }
  static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                        {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  const Meta meta = MetaFor(style);
  const uint32_t color = Color32(style.color).Packed();
  for (const auto& edge : kEdges) lines_.push_back({{corners[edge[0]], corners[edge[1]], color, style.width}, meta});
  changed_ = true;
}

void DebugDraw::AddCircle(const Vec3& center, const Vec3& normal, float radius, const DrawStyle& style) {
  const float length = glm::length(normal);
  if (length <= 1e-6f) return;
  if (!RoomFor(lines_.size(), kMaxLines, kCircleSegments)) return;
  Vec3 u, v;
  PlaneAxes(normal / length, u, v);
  const Meta meta = MetaFor(style);
  const uint32_t color = Color32(style.color).Packed();
  Vec3 previous = center + u * radius;
  for (int i = 1; i <= kCircleSegments; ++i) {
    const float angle = kTau * static_cast<float>(i) / kCircleSegments;
    const Vec3 point = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
    lines_.push_back({{previous, point, color, style.width}, meta});
    previous = point;
  }
  changed_ = true;
}

void DebugDraw::AddSphere(const Vec3& center, float radius, const DrawStyle& style) {
  if (!RoomFor(lines_.size(), kMaxLines, 3 * kCircleSegments)) return;
  AddCircle(center, kRight, radius, style);
  AddCircle(center, kForward, radius, style);
  AddCircle(center, kUp, radius, style);
}

void DebugDraw::AddAxes(const Mat4& transform, float length, const DrawStyle& style) {
  if (!RoomFor(lines_.size(), kMaxLines, 3)) return;
  const Meta meta = MetaFor(style);
  const Vec3 origin = Transformed(transform, Vec3(0.0f));
  const Color axis_colors[3] = {colors::kRed, colors::kGreen, colors::kBlue};
  for (int axis = 0; axis < 3; ++axis) {
    Vec3 end(0.0f);
    end[axis] = length;
    const uint32_t color = Color32(axis_colors[axis].WithAlpha(style.color.a)).Packed();
    lines_.push_back({{origin, Transformed(transform, end), color, style.width}, meta});
  }
  changed_ = true;
}

void DebugDraw::AddCross(const Vec3& point, float size, const DrawStyle& style) {
  if (!RoomFor(lines_.size(), kMaxLines, 3)) return;
  const Meta meta = MetaFor(style);
  const uint32_t color = Color32(style.color).Packed();
  for (const Vec3& axis : {kRight, kForward, kUp}) {
    lines_.push_back({{point - axis * (0.5f * size), point + axis * (0.5f * size), color, style.width}, meta});
  }
  changed_ = true;
}

void DebugDraw::AddFrustum(const Mat4& view_projection, const DrawStyle& style) {
  if (!RoomFor(lines_.size(), kMaxLines, 12)) return;
  // The clip-space cube's corners back in the world (bit 0 = +x, bit 1 = +y, bit 2 = far).
  const Mat4 inverse = glm::inverse(view_projection);
  Vec3 corners[8];
  for (int i = 0; i < 8; ++i) {
    const Vec4 clip((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f, 1.0f);
    const Vec4 world = inverse * clip;
    corners[i] = Vec3(world) / world.w;
  }
  static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                        {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  const Meta meta = MetaFor(style);
  const uint32_t color = Color32(style.color).Packed();
  for (const auto& edge : kEdges) lines_.push_back({{corners[edge[0]], corners[edge[1]], color, style.width}, meta});
  changed_ = true;
}

void DebugDraw::AddTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const DrawStyle& style) {
  if (!RoomFor(triangles_.size(), kMaxTriangles, 1)) return;
  const Vec3 cross = glm::cross(b - a, c - a);
  const float area = glm::length(cross);
  // Lit from kShadeDirection, both sides alike; a degenerate triangle keeps its colour.
  const float facing = area > 1e-12f ? std::abs(glm::dot(cross / area, kShadeDirection)) : 1.0f;
  const float shade = 0.55f + 0.45f * facing;
  const Color shaded{style.color.r * shade, style.color.g * shade, style.color.b * shade, style.color.a};
  const uint32_t color = Color32(shaded).Packed();
  triangles_.push_back({{{{a, color}, {b, color}, {c, color}}}, MetaFor(style)});
  changed_ = true;
}

void DebugDraw::AddText(const Vec3& position, std::string_view text, const DrawStyle& style) {
  if (text.empty() || !RoomFor(texts_.size(), kMaxTexts, 1)) return;
  texts_.push_back({{position, Color32(style.color).Packed(), std::string(text)}, MetaFor(style)});
  changed_ = true;
}

template <typename Remove>
void DebugDraw::RemoveWhere(Remove remove) {
  size_t removed = std::erase_if(lines_, [&](const StoredLine& line) { return remove(line.meta); });
  removed += std::erase_if(triangles_, [&](const StoredTriangle& triangle) { return remove(triangle.meta); });
  removed += std::erase_if(texts_, [&](const StoredText& text) { return remove(text.meta); });
  if (removed > 0) changed_ = true;
}

void DebugDraw::RebuildLists() {
  changed_ = false;
  lists_.version = ++g_last_version;
  // Counting sort by depth mode, keeping the order they were drawn in within a mode.
  auto group = [](const auto& stored, auto& out, auto& starts, auto&& write) {
    std::array<uint32_t, kDepthModes> counts{};
    for (const auto& shape : stored) ++counts[static_cast<size_t>(shape.meta.depth)];
    starts[0] = 0;
    for (size_t mode = 0; mode < kDepthModes; ++mode) starts[mode + 1] = starts[mode] + counts[mode];
    std::array<uint32_t, kDepthModes> next{};
    for (size_t mode = 0; mode < kDepthModes; ++mode) next[mode] = starts[mode];
    for (const auto& shape : stored) write(out, next[static_cast<size_t>(shape.meta.depth)]++, shape);
  };
  lists_.lines.resize(lines_.size());
  group(lines_, lists_.lines, lists_.line_starts,
        [](std::vector<DebugLine>& out, uint32_t at, const StoredLine& line) { out[at] = line.line; });
  lists_.vertices.resize(triangles_.size() * 3);
  group(triangles_, lists_.vertices, lists_.triangle_starts,
        [](std::vector<DebugVertex>& out, uint32_t at, const StoredTriangle& triangle) {
          std::copy(triangle.corners.begin(), triangle.corners.end(), out.begin() + static_cast<ptrdiff_t>(at) * 3);
        });
}

void DebugDraw::InternalAccess::BeginFrame(float game_dt, bool moves) const {
  DebugDraw& d = draw_;
  d.now_ += std::max(game_dt, 0.0f);
  d.moving_ = moves;
  d.in_fixed_step_ = false;
  const double now = d.now_;
  d.RemoveWhere([moves, now](const Meta& meta) {
    switch (meta.lifetime) {
      case Lifetime::Frame: return moves;  // paused: the last frame's shapes stay
      case Lifetime::PausedFrame: return true;
      case Lifetime::FixedStep: return false;
      case Lifetime::Timed: return meta.expires <= now;
    }
    return true;
  });
}

void DebugDraw::InternalAccess::BeginFixedStep() const {
  draw_.RemoveWhere([](const Meta& meta) { return meta.lifetime == Lifetime::FixedStep; });
  draw_.in_fixed_step_ = true;
}

}  // namespace wlw::debug

#endif  // WLW_DEBUG_DRAW
