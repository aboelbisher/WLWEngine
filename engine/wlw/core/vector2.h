#pragma once

// Deprecated in 0.3: use wlw::Vec2 (wlw/core/math.h, = glm::vec2). Converts to and from it, so code written
// against the 0.2 API (core::Vector2 size = window.GetSize()) keeps compiling until it moves.
#include <glm/glm.hpp>

#include "wlw/core/deprecated.h"

#if !WLW_NO_DEPRECATED
namespace wlw::core {

struct WLW_DEPRECATED(0.3, "Use wlw::Vec2 (wlw/core/math.h)") Vector2 {
  float x = 0.0f;
  float y = 0.0f;

  constexpr Vector2() = default;
  constexpr Vector2(float x_, float y_) : x(x_), y(y_) {}
  constexpr Vector2(const glm::vec2& v) : x(v.x), y(v.y) {}  // NOLINT: implicit on purpose (a shim)
  constexpr operator glm::vec2() const { return {x, y}; }    // NOLINT
};

}  // namespace wlw::core
#endif
