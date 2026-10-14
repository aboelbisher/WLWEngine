#include "wlw/ui/icons.h"

#include <algorithm>
#include <cmath>

namespace wlw::ui {

namespace {

constexpr float kPi = 3.14159265f;

// The direction of an angle on the canvas: 0 = up, clockwise (y is down), like Canvas::Arc.
glm::vec2 Dir(float angle) { return {std::sin(angle), -std::cos(angle)}; }

// An arrow head at angle `at` on a circle, pointing along it (clockwise if `clockwise`).
void ArrowHead(Canvas& canvas, glm::vec2 center, float radius, float at, bool clockwise, float size,
               const core::Color& color) {
  const glm::vec2 point = center + Dir(at) * radius;
  const glm::vec2 radial = Dir(at);
  const glm::vec2 along = glm::vec2(-radial.y, radial.x) * (clockwise ? 1.0f : -1.0f);  // d/da of Dir
  canvas.Triangle(point + along * size, point + radial * size * 0.85f - along * size * 0.15f,
                  point - radial * size * 0.85f - along * size * 0.15f, color);
}

}  // namespace

void DrawIcon(Canvas& canvas, Icon icon, const Rect& box, const core::Color& color) {
  // Shapes are written in a square from -1 to 1 (y down), scaled to the box.
  const glm::vec2 c = box.Center();
  const float s = std::min(box.Size().x, box.Size().y) * 0.5f;
  const auto P = [&](float x, float y) { return c + glm::vec2(x, y) * s; };
  const auto R = [&](float x0, float y0, float x1, float y1) { return Rect{P(x0, y0), P(x1, y1)}; };
  const auto line = [&](glm::vec2 a, glm::vec2 b, float w) { canvas.Line(a, b, w * s, color); };

  switch (icon) {
    case Icon::None: break;
    case Icon::Play: canvas.Triangle(P(-0.5f, -0.78f), P(0.82f, 0.0f), P(-0.5f, 0.78f), color); break;
    case Icon::Pause:
      canvas.FillRect(R(-0.62f, -0.72f, -0.16f, 0.72f), color, 0.12f * s);
      canvas.FillRect(R(0.16f, -0.72f, 0.62f, 0.72f), color, 0.12f * s);
      break;
    case Icon::Back:
      line(P(0.28f, -0.66f), P(-0.36f, 0.0f), 0.32f);
      line(P(-0.36f, 0.0f), P(0.28f, 0.66f), 0.32f);
      break;
    case Icon::Next:
      line(P(-0.28f, -0.66f), P(0.36f, 0.0f), 0.32f);
      line(P(0.36f, 0.0f), P(-0.28f, 0.66f), 0.32f);
      break;
    case Icon::Close:
      line(P(-0.55f, -0.55f), P(0.55f, 0.55f), 0.32f);
      line(P(0.55f, -0.55f), P(-0.55f, 0.55f), 0.32f);
      break;
    case Icon::Check:
      line(P(-0.62f, 0.02f), P(-0.16f, 0.5f), 0.32f);
      line(P(-0.16f, 0.5f), P(0.66f, -0.5f), 0.32f);
      break;
    case Icon::Settings:
      canvas.Ring(c, 0.48f * s, 0.3f * s, color);
      for (int k = 0; k < 8; ++k) {
        const glm::vec2 d = Dir(static_cast<float>(k) * kPi / 4.0f);
        canvas.Line(c + d * (0.6f * s), c + d * (0.8f * s), 0.32f * s, color);
      }
      break;
    case Icon::Music:
      canvas.Circle(P(-0.42f, 0.52f), 0.24f * s, color);
      canvas.Circle(P(0.44f, 0.36f), 0.24f * s, color);
      line(P(-0.2f, 0.5f), P(-0.2f, -0.52f), 0.15f);
      line(P(0.66f, 0.34f), P(0.66f, -0.68f), 0.15f);
      line(P(-0.2f, -0.52f), P(0.66f, -0.68f), 0.28f);
      break;
    case Icon::Sound:
      canvas.FillRect(R(-0.82f, -0.28f, -0.38f, 0.28f), color, 0.08f * s);
      canvas.Triangle(P(-0.46f, -0.28f), P(0.06f, -0.74f), P(0.06f, 0.74f), color);
      canvas.Triangle(P(-0.46f, -0.28f), P(0.06f, 0.74f), P(-0.46f, 0.28f), color);
      canvas.Arc(P(0.06f, 0.0f), 0.38f * s, 0.15f * s, kPi / 2.0f - 0.75f, 1.5f, color);
      canvas.Arc(P(0.06f, 0.0f), 0.7f * s, 0.15f * s, kPi / 2.0f - 0.8f, 1.6f, color);
      break;
    case Icon::Vibration:
      canvas.FillRect(R(-0.3f, -0.72f, 0.3f, 0.72f), color, 0.14f * s);
      for (float side : {-1.0f, 1.0f}) {
        const float a = 0.52f * side, b = 0.72f * side;
        line(P(a, -0.42f), P(b, -0.21f), 0.12f);
        line(P(b, -0.21f), P(a, 0.0f), 0.12f);
        line(P(a, 0.0f), P(b, 0.21f), 0.12f);
        line(P(b, 0.21f), P(a, 0.42f), 0.12f);
      }
      break;
    case Icon::Trophy:
      canvas.FillRect(R(-0.5f, -0.76f, 0.5f, 0.16f), color, 0.46f * s);    // the bowl's round bottom
      canvas.FillRect(R(-0.5f, -0.76f, 0.5f, -0.3f), color, 0.06f * s);    // ...and its square top
      canvas.Arc(P(-0.5f, -0.4f), 0.24f * s, 0.13f * s, kPi, kPi, color);  // handles
      canvas.Arc(P(0.5f, -0.4f), 0.24f * s, 0.13f * s, 0.0f, kPi, color);
      line(P(0.0f, 0.1f), P(0.0f, 0.48f), 0.2f);
      canvas.FillRect(R(-0.42f, 0.46f, 0.42f, 0.76f), color, 0.08f * s);
      break;
    case Icon::Help: {
      const glm::vec2 top = P(0.0f, -0.32f);
      const float end = 2.05f;  // the curl ends at the lower right...
      canvas.Arc(top, 0.34f * s, 0.24f * s, -kPi / 2.0f, end + kPi / 2.0f, color);
      line(top + Dir(end) * (0.34f * s), P(0.0f, 0.2f), 0.24f);  // ...and comes down to the middle
      canvas.Circle(P(0.0f, 0.62f), 0.15f * s, color);
      break;
    }
    case Icon::Restart: {
      const float start = 0.9f, sweep = 2.0f * kPi - 1.25f;
      canvas.Arc(c, 0.55f * s, 0.22f * s, start, sweep, color);
      ArrowHead(canvas, c, 0.55f * s, start + sweep, true, 0.36f * s, color);
      break;
    }
    case Icon::Undo: {
      const glm::vec2 center = P(0.1f, 0.12f);
      const float start = -kPi / 2.0f + 0.15f, sweep = kPi + 0.45f;
      canvas.Arc(center, 0.55f * s, 0.22f * s, start, sweep, color);
      ArrowHead(canvas, center, 0.55f * s, start, false, 0.36f * s, color);
      break;
    }
    case Icon::Hint:
      canvas.Circle(P(0.0f, -0.22f), 0.5f * s, color);
      canvas.FillRect(R(-0.25f, 0.12f, 0.25f, 0.5f), color, 0.06f * s);
      canvas.FillRect(R(-0.18f, 0.56f, 0.18f, 0.78f), color, 0.09f * s);
      break;
    case Icon::Star: {
      // Five triangles, each a point plus part of the middle: together the star, with every outside
      // edge drawn once (overlaps only inside, where they're fully covered: no seams).
      glm::vec2 outer[5], inner[5];
      for (int k = 0; k < 5; ++k) {
        const float a = static_cast<float>(k) * 2.0f * kPi / 5.0f;
        outer[k] = c + Dir(a) * (0.98f * s) + glm::vec2(0.0f, 0.06f * s);
        inner[k] = c + Dir(a + kPi / 5.0f) * (0.4f * s) + glm::vec2(0.0f, 0.06f * s);
      }
      for (int k = 0; k < 5; ++k) canvas.Triangle(outer[k], inner[(k + 1) % 5], inner[(k + 3) % 5], color);
      break;
    }
    case Icon::Lock:
      canvas.Arc(P(0.0f, -0.22f), 0.34f * s, 0.17f * s, -kPi / 2.0f, kPi, color);
      line(P(-0.34f, -0.22f), P(-0.34f, 0.0f), 0.17f);
      line(P(0.34f, -0.22f), P(0.34f, 0.0f), 0.17f);
      canvas.FillRect(R(-0.56f, -0.04f, 0.56f, 0.76f), color, 0.14f * s);
      break;
    case Icon::Clock:
      canvas.Ring(c, 0.68f * s, 0.2f * s, color);
      line(c, P(0.0f, -0.38f), 0.17f);
      line(c, P(0.3f, 0.0f), 0.17f);
      break;
    case Icon::Privacy:
      canvas.FillRect(R(-0.62f, -0.76f, 0.62f, 0.06f), color, 0.12f * s);
      canvas.Triangle(P(-0.62f, 0.0f), P(0.62f, 0.0f), P(0.0f, 0.8f), color);
      break;
    case Icon::Home:
      canvas.Triangle(P(-0.82f, -0.04f), P(0.0f, -0.8f), P(0.82f, -0.04f), color);
      canvas.FillRect(R(-0.56f, -0.12f, 0.56f, 0.76f), color, 0.08f * s);
      break;
    case Icon::Grid:
      for (float y : {-0.4f, 0.4f}) {
        for (float x : {-0.4f, 0.4f}) canvas.FillRect(R(x - 0.33f, y - 0.33f, x + 0.33f, y + 0.33f), color, 0.1f * s);
      }
      break;
  }
}

}  // namespace wlw::ui
