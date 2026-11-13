#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/core/color.h"
#include "wlw/core/geometry.h"
#include "wlw/core/math.h"

// Debug drawing (ROADMAP 13.10, early slice): lines, shapes, triangles and labels in the 3D world, drawn over the
// scene for a frame or a while, from anywhere in game code. Like Unreal's DrawDebugLine, Unity's Debug.DrawLine and
// Gizmos, Godot's DebugDraw3D: the shapes go into one list per app, App().DebugDraw(), and the renderer draws the
// list in every viewport that shows it (Viewport::SetShowDebugDraw; the screen does).
//
//   App().DebugDraw().Line(a, b, {.color = colors::kRed, .width = 3.0f});
//   App().DebugDraw().Box(world_box, {.depth = debug::DepthMode::XRay, .seconds = 2.0f});
//
// Only in builds with WLW_DEBUG_DRAW (on unless the build is a release type, like the debug UI): elsewhere the calls
// are empty, debug_draw.cpp and the renderer's part compile to nothing and the shaders aren't embedded. Work done only
// to feed it belongs under `if constexpr (debug::kDebugDrawEnabled)`. The cvar debug.draw turns it off at run time.
//
// Lifetimes: a shape lasts until the next frame starts, or `seconds` of game time. One drawn from OnFixedUpdate
// lasts until the next fixed step, so it neither flickers on frames without a step nor piles up on frames with two
// (Unity's FixedUpdate lines flicker). While the game is paused (the Studio's Pause) the last frame's shapes stay.
// Drawn after the scene's transparent objects, before 2D and post passes; labels (Text) on top of everything, on the
// screen only (they're drawn by the debug UI).
namespace wlw::debug {

inline constexpr bool kDebugDrawEnabled = WLW_DEBUG_DRAW != 0;

enum class DepthMode : uint8_t {
  Tested,  // hidden behind what is in front of it (the default)
  XRay,    // the same, plus the hidden parts drawn faint (e.g. joints inside a build)
  OnTop,   // never hidden
};
inline constexpr int kDepthModes = 3;

struct DrawStyle {
  Color color = colors::kWhite;  // straight alpha; triangles get darker the more they face away from the light
  float width = 2.0f;            // lines: dp (screen pixels at 1x, scaled for the screen like the debug UI's text)
  DepthMode depth = DepthMode::Tested;
  float seconds = 0.0f;  // 0: until the next frame (from OnFixedUpdate: the next fixed step); > 0: game seconds
};

// A line as the GPU reads it: one instance of the line quad (shaders/debug_line.vert), 32 bytes.
struct DebugLine {
  Vec3 a{0.0f};
  Vec3 b{0.0f};
  uint32_t color = 0;  // Color32::Packed, straight alpha
  float width = 0.0f;  // dp
};
static_assert(sizeof(DebugLine) == 32);

// A triangle corner as the GPU reads it (shaders/debug_triangle.vert), 16 bytes; its colour is already shaded.
struct DebugVertex {
  Vec3 position{0.0f};
  uint32_t color = 0;
};
static_assert(sizeof(DebugVertex) == 16);

struct DebugText {
  Vec3 position{0.0f};
  uint32_t color = 0;
  std::string text;
};

struct DebugDrawStats {
  int lines = 0;      // alive now (every lifetime)
  int triangles = 0;  //
  int texts = 0;      //
  int dropped = 0;    // shapes refused since the start: over the limits below
};

class DebugDraw {
 public:
  // Alive at once, every lifetime together; more are dropped (logged once, counted in Stats().dropped).
  static constexpr int kMaxLines = 65536;
  static constexpr int kMaxTriangles = 65536;
  static constexpr int kMaxTexts = 1024;

  // ---- Lines and wire shapes ----
  void Line(const Vec3& a, const Vec3& b, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddLine(a, b, style);
    }
  }
  // Each point to the next; `closed` joins the last to the first too.
  void Polyline(std::span<const Vec3> points, bool closed, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddPolyline(points, closed, style);
    }
  }
  // A line with a head at `to` (a quarter of its length, at most `head`).
  void Arrow(const Vec3& from, const Vec3& to, const DrawStyle& style = {}, float head = 0.25f) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddArrow(from, to, style, head);
    }
  }
  void Box(const AABB& box, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddBox(Mat4(1.0f), box.Center(), box.Extents(), style);
    }
  }
  // A box `half_extents` around `transform`'s origin, turned (and scaled) with it: a node's or body's box.
  void Box(const Mat4& transform, const Vec3& half_extents, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddBox(transform, Vec3(0.0f), half_extents, style);
    }
  }
  // Three circles, one around each axis.
  void Sphere(const Vec3& center, float radius, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddSphere(center, radius, style);
    }
  }
  void Circle(const Vec3& center, const Vec3& normal, float radius, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddCircle(center, normal, radius, style);
    }
  }
  // The transform's axes, `length` long: X red, Y green, Z blue (style.color unused; its alpha is).
  void Axes(const Mat4& transform, float length = 1.0f, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddAxes(transform, length, style);
    }
  }
  // Three lines through the point along the world axes, `size` long each.
  void Cross(const Vec3& point, float size, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddCross(point, size, style);
    }
  }
  // The view volume of a projection x view matrix (Camera3D::GetViewProjectionMatrix): its 12 edges.
  void Frustum(const Mat4& view_projection, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddFrustum(view_projection, style);
    }
  }

  // ---- Filled ----
  // Seen from both sides, flat; shaded by which way it faces so solids read as solids. See-through needs alpha.
  void Triangle(const Vec3& a, const Vec3& b, const Vec3& c, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddTriangle(a, b, c, style);
    }
  }

  // ---- Labels ----
  // Text with its left end at the point, always on top (the depth mode and width are unused). Screen only.
  void Text(const Vec3& position, std::string_view text, const DrawStyle& style = {}) {
    if constexpr (kDebugDrawEnabled) {
      if (enabled_) AddText(position, text, style);
    }
  }

  // ---- State ----
  // Off: every call is ignored and what was drawn is cleared (the cvar debug.draw).
  void SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled) Clear();
  }
  bool Enabled() const { return kDebugDrawEnabled && enabled_; }
  // Every shape, whatever its lifetime.
  void Clear() {
    lines_.clear();
    triangles_.clear();
    texts_.clear();
    changed_ = true;
  }
  DebugDrawStats Stats() const {
    return {static_cast<int>(lines_.size()), static_cast<int>(triangles_.size()), static_cast<int>(texts_.size()),
            dropped_};
  }

  class InternalAccess;
  InternalAccess Internal();

 private:
  // How long a shape stays.
  enum class Lifetime : uint8_t {
    Frame,        // until a frame in which the game moves starts
    PausedFrame,  // drawn while paused: until the next frame
    FixedStep,    // drawn from OnFixedUpdate: until the next fixed step
    Timed,        // until `expires` (game seconds)
  };
  struct Meta {
    double expires = 0.0;
    Lifetime lifetime = Lifetime::Frame;
    DepthMode depth = DepthMode::Tested;
  };
  struct StoredLine {
    DebugLine line;
    Meta meta;
  };
  struct StoredTriangle {
    std::array<DebugVertex, 3> corners;
    Meta meta;
  };
  struct StoredText {
    DebugText text;
    Meta meta;
  };

  Meta MetaFor(const DrawStyle& style) const;
  bool RoomFor(size_t stored, int max, int adding);
  void AddLine(const Vec3& a, const Vec3& b, const DrawStyle& style);
  void AddPolyline(std::span<const Vec3> points, bool closed, const DrawStyle& style);
  void AddArrow(const Vec3& from, const Vec3& to, const DrawStyle& style, float head);
  void AddBox(const Mat4& transform, const Vec3& center, const Vec3& half_extents, const DrawStyle& style);
  void AddSphere(const Vec3& center, float radius, const DrawStyle& style);
  void AddCircle(const Vec3& center, const Vec3& normal, float radius, const DrawStyle& style);
  void AddAxes(const Mat4& transform, float length, const DrawStyle& style);
  void AddCross(const Vec3& point, float size, const DrawStyle& style);
  void AddFrustum(const Mat4& view_projection, const DrawStyle& style);
  void AddTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const DrawStyle& style);
  void AddText(const Vec3& position, std::string_view text, const DrawStyle& style);
  // Removes the shapes `remove(meta)` says, noting a change.
  template <typename Remove>
  void RemoveWhere(Remove remove);
  void RebuildLists();

  bool enabled_ = true;
  bool moving_ = true;          // the game moves this frame (not paused)
  bool in_fixed_step_ = false;  // inside OnFixedUpdate
  double now_ = 0.0;            // game seconds since the start
  std::vector<StoredLine> lines_;
  std::vector<StoredTriangle> triangles_;
  std::vector<StoredText> texts_;
  int dropped_ = 0;
  bool warned_ = false;

  // What the renderer draws, rebuilt when the shapes changed: grouped by depth mode in DepthMode's order.
  struct DrawLists {
    std::vector<DebugLine> lines;
    std::array<uint32_t, kDepthModes + 1> line_starts{};  // mode m: [starts[m], starts[m + 1])
    std::vector<DebugVertex> vertices;                    // 3 per triangle
    std::array<uint32_t, kDepthModes + 1> triangle_starts{};
    uint64_t version = 0;  // new for every rebuild, unique among every DebugDraw
  };
  DrawLists lists_;
  bool changed_ = false;
};

// For the Application and the renderer, not games. BeginFrame and BeginFixedStep exist only in builds with
// WLW_DEBUG_DRAW (call them under `if constexpr (kDebugDrawEnabled)`); without it debug_draw.cpp is empty.
class DebugDraw::InternalAccess {
 public:
  explicit InternalAccess(DebugDraw& draw) : draw_(draw) {}

  // The start of a frame: `game_dt` of game time passed (0 while paused), `moves` = the game moves this frame. Ends
  // the shapes whose time is up.
  void BeginFrame(float game_dt, bool moves) const;
  // Around each OnFixedUpdate: the last step's shapes end; what's drawn now lasts until the next one.
  void BeginFixedStep() const;
  void EndFixedStep() const { draw_.in_fixed_step_ = false; }

  // The shapes to draw, grouped by depth mode (rebuilt here when they changed; always empty without debug draw).
  const DrawLists& Lists() const {
    if constexpr (kDebugDrawEnabled) {
      if (draw_.changed_) draw_.RebuildLists();
    }
    return draw_.lists_;
  }
  const std::vector<StoredText>& Texts() const { return draw_.texts_; }

 private:
  DebugDraw& draw_;
};

inline DebugDraw::InternalAccess DebugDraw::Internal() { return InternalAccess(*this); }

}  // namespace wlw::debug
