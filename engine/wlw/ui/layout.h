#pragma once

#include <algorithm>

#include <glm/glm.hpp>

#include "wlw/ui/canvas.h"

// Small layout helpers for ui::Canvas: computing rectangles to draw into. No state, no magic -
// enough for menus and HUDs without a layout engine.
namespace wlw::ui {

// A rectangle of `size` centred on `center`.
inline Rect Centered(glm::vec2 center, glm::vec2 size) { return {center - size * 0.5f, center + size * 0.5f}; }

// `rect` shrunk by `amount` on every side (negative grows it).
inline Rect Inset(const Rect& rect, float amount) { return {rect.min + glm::vec2(amount), rect.max - glm::vec2(amount)}; }

// Row `index` of `count` equal rows stacked top to bottom in `area`, `gap` apart.
inline Rect Row(const Rect& area, int index, int count, float gap = 10.0f) {
  count = std::max(count, 1);
  const float height = (area.Size().y - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
  const float top = area.min.y + static_cast<float>(index) * (height + gap);
  return {{area.min.x, top}, {area.max.x, top + height}};
}

// Column `index` of `count` equal columns side by side in `area`, `gap` apart.
inline Rect Column(const Rect& area, int index, int count, float gap = 10.0f) {
  count = std::max(count, 1);
  const float width = (area.Size().x - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
  const float left = area.min.x + static_cast<float>(index) * (width + gap);
  return {{left, area.min.y}, {left + width, area.max.y}};
}

}  // namespace wlw::ui
