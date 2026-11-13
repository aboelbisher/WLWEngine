#pragma once

#include <algorithm>

#include "wlw/core/math.h"
#include "wlw/input/input.h"
#include "wlw/scene/window.h"

namespace wlw::debug {

// Pixels per dp for debug drawing and the debug UI (its text and widgets, debug draw's line widths). Desktop: the
// OS's display scaling (ContentScale), in pointer units: where those are already larger than pixels (macOS points:
// the framebuffer scale draws them sharp) that part is taken out again. On a phone the surface is in physical pixels:
// ~400 px per short side ~ 1 "dp", a rough density estimate (a 1080 px wide phone gets ~2.7x) until the platform
// reports real density.
inline float DebugScale(const scene::Window& window) {
  const input::Input& input = window.GetInput();
  const Vec2 size = input.SurfaceSize();
  if (input.IsTouchScreen()) return std::clamp(std::min(size.x, size.y) / 400.0f, 1.0f, 4.0f);
  const Vec2 framebuffer = window.GetSize();
  const float points = size.x > 0.0f && framebuffer.x > 0.0f ? framebuffer.x / size.x : 1.0f;
  return std::clamp(window.ContentScale() / points, 1.0f, 4.0f);
}

}  // namespace wlw::debug
