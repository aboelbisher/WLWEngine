#pragma once

#include <span>
#include <string_view>

#include <glm/glm.hpp>

#include "wlw/core/math.h"
#include "wlw/platform/platform.h"

namespace wlw::studio {

// A screen the Game panel simulates (Unity's Device Simulator, a browser's device toolbar): its pixels held
// upright (portrait for phones and tablets), the parts the system covers there (status bar, notch, home
// indicator) and whether it is a touch screen. Approximate public figures, enough to lay out UI against.
struct DevicePreset {
  std::string_view name;
  glm::ivec2 size{0, 0};               // 0 x 0 = "Free": the Game panel's own size
  platform::SafeAreaInsets safe_area;  // upright, in pixels
  bool touch = false;
};

// The Studio's devices: Free first, then desktop sizes, phones and a tablet.
std::span<const DevicePreset> DevicePresets();

// What the game's window gets from a preset.
struct DeviceScreen {
  glm::ivec2 size{1, 1};
  platform::SafeAreaInsets safe_area;
  bool touch = false;
};

// The preset upright, or turned a quarter left (a phone in landscape: its top edge and notch go to the left side; a
// desktop size in portrait). Free takes `free_size` (the panel's, in pixels) and never turns.
DeviceScreen ScreenFor(const DevicePreset& preset, bool turned, glm::ivec2 free_size);

// Where the game's picture is drawn in the Game panel, in the Studio window's pointer units (= ImGui's).
struct GameRect {
  Vec2 min{0.0f};
  Vec2 size{0.0f};
  bool Contains(Vec2 point) const {
    return point.x >= min.x && point.y >= min.y && point.x < min.x + size.x && point.y < min.y + size.y;
  }
};

// The device's screen fitted inside `area` (the panel's free space): as large as fits, aspect kept, centered;
// never larger than its own pixels times `max_zoom`.
GameRect FitGameRect(Vec2 area_min, Vec2 area_size, glm::ivec2 device, float max_zoom);

// A point in the Studio window (pointer units) -> the game's pixels (outside the picture: beyond its edges).
Vec2 ToDevicePixels(const GameRect& rect, glm::ivec2 device, Vec2 point);

}  // namespace wlw::studio
