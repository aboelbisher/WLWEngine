// Tests for debug drawing (debug/debug_draw.h): the shapes' lines, lifetimes (frame, fixed step, timed, paused frames),
// the draw lists grouped by depth mode, triangle shading, the limits and turning it off. CPU only; the pictures are
// in tests/gpu (gpu: debug draw).
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "test_helpers.h"
#include "wlw/debug/debug_draw.h"
#include "wlw/scene/camera_3d.h"

TEST_SUITE_BEGIN("debug_draw");

using namespace wlw;
using debug::DebugDraw;
using debug::DepthMode;

#if WLW_DEBUG_DRAW

namespace {

int Lines(DebugDraw& draw) { return static_cast<int>(draw.Internal().Lists().lines.size()); }
int Triangles(DebugDraw& draw) { return static_cast<int>(draw.Internal().Lists().vertices.size() / 3); }

// Every end point of the lines drawn.
std::vector<Vec3> Ends(DebugDraw& draw) {
  std::vector<Vec3> ends;
  for (const debug::DebugLine& line : draw.Internal().Lists().lines) {
    ends.push_back(line.a);
    ends.push_back(line.b);
  }
  return ends;
}

bool Near(const Vec3& a, const Vec3& b, float tolerance = 1e-4f) { return glm::length(a - b) <= tolerance; }

}  // namespace

TEST_CASE("shapes are made of lines: box, sphere, circle, arrow, axes, cross, polyline") {
  DebugDraw draw;
  draw.Box(AABB{{-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}});
  CHECK(Lines(draw) == 12);
  for (const Vec3& end : Ends(draw)) {
    CHECK(std::abs(end.x) == doctest::Approx(1.0f));
    CHECK(std::abs(end.y) == doctest::Approx(2.0f));
    CHECK(std::abs(end.z) == doctest::Approx(3.0f));
  }
  // Each edge is along one axis, as long as the box is that way.
  for (const debug::DebugLine& line : draw.Internal().Lists().lines) {
    const Vec3 d = glm::abs(line.b - line.a);
    const int axes = (d.x > 1e-5f) + (d.y > 1e-5f) + (d.z > 1e-5f);
    CHECK(axes == 1);
  }

  draw.Clear();
  // A turned box: its corners turn with the transform.
  const Mat4 turned = glm::translate(Mat4(1.0f), Vec3(5.0f, 0.0f, 0.0f)) * glm::rotate(Mat4(1.0f), kHalfPi, kUp);
  draw.Box(turned, Vec3(1.0f, 0.5f, 0.25f));
  for (const Vec3& end : Ends(draw)) {
    CHECK(std::abs(end.x - 5.0f) == doctest::Approx(0.5f));  // the box's y half extent now runs along x
    CHECK(std::abs(end.y) == doctest::Approx(1.0f));
  }

  draw.Clear();
  draw.Circle({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 2.0f}, 1.5f);  // the normal needn't be unit length
  CHECK(Lines(draw) == 32);
  for (const Vec3& end : Ends(draw)) {
    CHECK(glm::length(end - Vec3(1.0f, 2.0f, 3.0f)) == doctest::Approx(1.5f));
    CHECK(end.z == doctest::Approx(3.0f));  // in the plane across the normal
  }
  draw.Clear();
  draw.Sphere({0.0f, 0.0f, 0.0f}, 2.0f);
  CHECK(Lines(draw) == 3 * 32);
  for (const Vec3& end : Ends(draw)) CHECK(glm::length(end) == doctest::Approx(2.0f));

  draw.Clear();
  draw.Arrow({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 4.0f});
  CHECK(Lines(draw) == 5);  // the shaft and a 4-line head
  for (const debug::DebugLine& line : draw.Internal().Lists().lines) {
    CHECK(std::max(line.a.z, line.b.z) == doctest::Approx(4.0f));  // everything meets at the tip
  }

  draw.Clear();
  draw.Axes(glm::translate(Mat4(1.0f), Vec3(1.0f, 1.0f, 1.0f)), 2.0f, {.color = colors::kWhite.WithAlpha(0.5f)});
  const auto& axes = draw.Internal().Lists().lines;
  REQUIRE(axes.size() == 3);
  CHECK(Near(axes[0].b, {3.0f, 1.0f, 1.0f}));
  CHECK(Near(axes[1].b, {1.0f, 3.0f, 1.0f}));
  CHECK(Near(axes[2].b, {1.0f, 1.0f, 3.0f}));
  CHECK(axes[0].color == Color32(colors::kRed.WithAlpha(0.5f)).Packed());  // x red, its alpha from the style
  CHECK(axes[2].color == Color32(colors::kBlue.WithAlpha(0.5f)).Packed());

  draw.Clear();
  draw.Cross({1.0f, 2.0f, 3.0f}, 0.5f);
  CHECK(Lines(draw) == 3);
  for (const debug::DebugLine& line : draw.Internal().Lists().lines) {
    CHECK(glm::length(line.b - line.a) == doctest::Approx(0.5f));
    CHECK(Near((line.a + line.b) * 0.5f, {1.0f, 2.0f, 3.0f}));
  }

  draw.Clear();
  const std::array<Vec3, 4> square = {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0), Vec3(0, 1, 0)};
  draw.Polyline(square, /*closed=*/false);
  CHECK(Lines(draw) == 3);
  draw.Polyline(square, /*closed=*/true);
  CHECK(Lines(draw) == 3 + 4);
  draw.Polyline(std::span<const Vec3>(square.data(), 1), true);  // one point: nothing
  CHECK(Lines(draw) == 7);
}

TEST_CASE("a frustum's lines run along the camera's view volume") {
  scene::Camera3D camera;
  camera.SetPerspective(60.0f * kDegToRad, 0.5f, 20.0f);
  camera.SetPosition({0.0f, -10.0f, 5.0f});
  camera.LookAt({0.0f, 0.0f, 0.0f});
  const Mat4 view_projection = camera.GetViewProjectionMatrix(1.5f);
  DebugDraw draw;
  draw.Frustum(view_projection);
  REQUIRE(Lines(draw) == 12);
  // Every corner lands on a corner of the clip-space cube.
  for (const Vec3& end : Ends(draw)) {
    const Vec4 clip = view_projection * Vec4(end, 1.0f);
    const Vec3 ndc = Vec3(clip) / clip.w;
    CHECK(std::abs(ndc.x) == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(std::abs(ndc.y) == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(std::abs(ndc.z) == doctest::Approx(1.0f).epsilon(1e-3));
  }
}

TEST_CASE("lifetimes: a frame, game seconds, the next fixed step; paused frames keep the last frame's shapes") {
  DebugDraw draw;
  auto frame = [&draw](float dt, bool moves = true) { draw.Internal().BeginFrame(dt, moves); };

  // This frame only.
  frame(0.016f);
  draw.Line({0, 0, 0}, {1, 0, 0});
  CHECK(Lines(draw) == 1);
  frame(0.016f);
  CHECK(Lines(draw) == 0);

  // Timed: game seconds.
  draw.Line({0, 0, 0}, {1, 0, 0}, {.seconds = 0.1f});
  frame(0.06f);
  CHECK(Lines(draw) == 1);
  frame(0.0f, false);  // paused: game time stands still
  CHECK(Lines(draw) == 1);
  frame(0.06f);
  CHECK(Lines(draw) == 0);

  // From a fixed step: until the next fixed step, whatever the frames in between.
  frame(0.016f);
  draw.Internal().BeginFixedStep();
  draw.Line({0, 0, 0}, {0, 1, 0});
  draw.Internal().EndFixedStep();
  draw.Line({0, 0, 0}, {0, 0, 1});  // after the step, in OnUpdate: this frame only
  CHECK(Lines(draw) == 2);
  frame(0.016f);  // a frame without a fixed step: the step's line stays
  CHECK(Lines(draw) == 1);
  frame(0.016f);
  for (int step = 0; step < 2; ++step) {  // a frame with two steps: only the last one's line is left
    draw.Internal().BeginFixedStep();
    draw.Line({0, 0, 0}, {0, 1, static_cast<float>(step)});
    draw.Internal().EndFixedStep();
  }
  REQUIRE(Lines(draw) == 1);
  CHECK(draw.Internal().Lists().lines[0].b.z == 1.0f);

  // Paused: what the last moving frame drew stays; what's drawn while paused lasts one frame.
  draw.Clear();
  frame(0.016f);
  draw.Line({0, 0, 0}, {1, 1, 1});
  frame(0.0f, false);
  CHECK(Lines(draw) == 1);
  draw.Line({0, 0, 0}, {2, 2, 2});  // e.g. from OnDebugUI
  CHECK(Lines(draw) == 2);
  frame(0.0f, false);
  CHECK(Lines(draw) == 1);
  frame(0.016f);  // moving again
  CHECK(Lines(draw) == 0);
}

TEST_CASE("draw lists: grouped by depth mode in drawing order; rebuilt only after a change") {
  DebugDraw draw;
  draw.Line({0, 0, 0}, {1, 0, 0}, {.depth = DepthMode::OnTop});
  draw.Line({0, 0, 0}, {2, 0, 0}, {.depth = DepthMode::Tested});
  draw.Line({0, 0, 0}, {3, 0, 0}, {.depth = DepthMode::XRay});
  draw.Line({0, 0, 0}, {4, 0, 0}, {.depth = DepthMode::Tested});
  draw.Triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {.depth = DepthMode::XRay});
  const auto& lists = draw.Internal().Lists();
  CHECK(lists.line_starts == std::array<uint32_t, 4>{0, 2, 3, 4});
  REQUIRE(lists.lines.size() == 4);
  CHECK(lists.lines[0].b.x == 2.0f);  // Tested, in order
  CHECK(lists.lines[1].b.x == 4.0f);
  CHECK(lists.lines[2].b.x == 3.0f);  // XRay
  CHECK(lists.lines[3].b.x == 1.0f);  // OnTop
  CHECK(lists.triangle_starts == std::array<uint32_t, 4>{0, 0, 1, 1});
  CHECK(lists.vertices.size() == 3);

  const uint64_t version = lists.version;
  CHECK(draw.Internal().Lists().version == version);  // nothing changed
  draw.Internal().BeginFrame(0.016f, true);           // the shapes end: a change
  CHECK(draw.Internal().Lists().version != version);
  CHECK(draw.Internal().Lists().lines.empty());
  // Another DebugDraw's versions never repeat this one's (the renderer tells uploads apart by them).
  DebugDraw other;
  other.Line({0, 0, 0}, {1, 1, 1});
  CHECK(other.Internal().Lists().version > draw.Internal().Lists().version);
}

TEST_CASE("lines keep their colour and width; triangles are shaded by facing, alpha kept") {
  DebugDraw draw;
  draw.Line({0, 0, 0}, {1, 0, 0}, {.color = Color(0.2f, 0.4f, 0.6f, 0.8f), .width = 3.5f});
  const debug::DebugLine& line = draw.Internal().Lists().lines.at(0);
  CHECK(line.color == Color32(Color(0.2f, 0.4f, 0.6f, 0.8f)).Packed());
  CHECK(line.width == 3.5f);

  // Facing up (towards the light, which is mostly above) is brighter than standing upright facing sideways.
  const Color red(1.0f, 0.0f, 0.0f, 0.5f);
  draw.Triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {.color = red});  // flat, facing up
  draw.Triangle({0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {.color = red});  // the same, wound the other way: the same shade
  draw.Triangle({0, 0, 0}, {0, 0, 1}, {0, 1, 0}, {.color = red});  // upright, facing +x
  const auto& corners = draw.Internal().Lists().vertices;
  REQUIRE(corners.size() == 9);
  const Color32 up = Color32::FromPacked(corners[0].color);
  const Color32 down = Color32::FromPacked(corners[3].color);
  const Color32 side = Color32::FromPacked(corners[6].color);
  CHECK(up == down);
  CHECK(up.r > side.r);
  CHECK(up.a == Color32(red).a);
  CHECK(side.a == Color32(red).a);
  CHECK(corners[0].color == corners[1].color);  // one colour per triangle
}

TEST_CASE("limits: whole shapes are refused when full and counted") {
  DebugDraw draw;
  for (int i = 0; i < DebugDraw::kMaxLines - 5; ++i) draw.Line({0, 0, 0}, {1, 0, 0});
  draw.Box(AABB{{0, 0, 0}, {1, 1, 1}});  // 12 lines don't fit in the 5 left: none drawn
  CHECK(Lines(draw) == DebugDraw::kMaxLines - 5);
  CHECK(draw.Stats().dropped == 1);
  draw.Cross({0, 0, 0}, 1.0f);  // 3 do
  CHECK(Lines(draw) == DebugDraw::kMaxLines - 2);
  CHECK(draw.Stats().lines == DebugDraw::kMaxLines - 2);
  for (int i = 0; i < DebugDraw::kMaxTexts + 3; ++i) draw.Text({0, 0, 0}, "label");
  CHECK(draw.Stats().texts == DebugDraw::kMaxTexts);
  CHECK(draw.Stats().dropped == 4);
  draw.Text({0, 0, 0}, "");  // empty: nothing, not even dropped
  CHECK(draw.Stats().dropped == 4);
}

TEST_CASE("turned off: nothing is kept, and what was drawn is cleared") {
  DebugDraw draw;
  draw.Line({0, 0, 0}, {1, 0, 0});
  draw.Triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0});
  draw.Text({0, 0, 0}, "a");
  CHECK(draw.Enabled());
  draw.SetEnabled(false);
  CHECK_FALSE(draw.Enabled());
  CHECK(Lines(draw) == 0);
  CHECK(Triangles(draw) == 0);
  CHECK(draw.Stats().texts == 0);
  draw.Sphere({0, 0, 0}, 1.0f);
  CHECK(Lines(draw) == 0);
  draw.SetEnabled(true);
  draw.Sphere({0, 0, 0}, 1.0f);
  CHECK(Lines(draw) == 96);
}

#else  // !WLW_DEBUG_DRAW

TEST_CASE("builds without debug draw keep nothing") {
  DebugDraw draw;
  CHECK_FALSE(draw.Enabled());
  draw.Line({0, 0, 0}, {1, 0, 0});
  draw.Box(AABB{{0, 0, 0}, {1, 1, 1}});
  draw.Triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0});
  draw.Text({0, 0, 0}, "a");
  const debug::DebugDrawStats stats = draw.Stats();
  CHECK(stats.lines == 0);
  CHECK(stats.triangles == 0);
  CHECK(stats.texts == 0);
}

#endif  // WLW_DEBUG_DRAW

TEST_SUITE_END();
