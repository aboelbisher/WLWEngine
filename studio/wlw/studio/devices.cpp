#include "wlw/studio/devices.h"

#include <algorithm>
#include <array>

namespace wlw::studio {

namespace {

// Pixels upright; insets = the OS's safe area in points x the screen's scale.
constexpr std::array kPresets = {
    DevicePreset{.name = "Free (panel size)"},
    DevicePreset{.name = "Desktop 1280 x 720", .size = {1280, 720}},
    DevicePreset{.name = "Desktop 1920 x 1080", .size = {1920, 1080}},
    DevicePreset{.name = "Pixel 4", .size = {1080, 2280}, .safe_area = {0, 88, 0, 66}, .touch = true},
    DevicePreset{.name = "Pixel 8", .size = {1080, 2400}, .safe_area = {0, 128, 0, 63}, .touch = true},
    DevicePreset{.name = "iPhone 15", .size = {1179, 2556}, .safe_area = {0, 177, 0, 102}, .touch = true},
    DevicePreset{.name = "iPhone SE", .size = {750, 1334}, .safe_area = {0, 40, 0, 0}, .touch = true},
    DevicePreset{.name = "iPad (10th gen)", .size = {1640, 2360}, .safe_area = {0, 48, 0, 40}, .touch = true},
};

}  // namespace

std::span<const DevicePreset> DevicePresets() { return kPresets; }

DeviceScreen ScreenFor(const DevicePreset& preset, bool turned, glm::ivec2 free_size) {
  DeviceScreen screen;
  screen.touch = preset.touch;
  if (preset.size.x <= 0 || preset.size.y <= 0) {
    screen.size = glm::max(free_size, glm::ivec2(1));
    return screen;
  }
  screen.size = preset.size;
  screen.safe_area = preset.safe_area;
  if (turned) {
    const platform::SafeAreaInsets upright = preset.safe_area;
    screen.size = {preset.size.y, preset.size.x};
    screen.safe_area = {.left = upright.top, .top = upright.right, .right = upright.bottom, .bottom = upright.left};
  }
  return screen;
}

GameRect FitGameRect(Vec2 area_min, Vec2 area_size, glm::ivec2 device, float max_zoom) {
  if (device.x <= 0 || device.y <= 0 || area_size.x <= 0.0f || area_size.y <= 0.0f) return {area_min, Vec2(0.0f)};
  const Vec2 pixels(static_cast<float>(device.x), static_cast<float>(device.y));
  const float zoom = std::min({area_size.x / pixels.x, area_size.y / pixels.y, max_zoom});
  const Vec2 size = pixels * zoom;
  return {area_min + (area_size - size) * 0.5f, size};
}

Vec2 ToDevicePixels(const GameRect& rect, glm::ivec2 device, Vec2 point) {
  if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) return Vec2(0.0f);
  return (point - rect.min) / rect.size * Vec2(static_cast<float>(device.x), static_cast<float>(device.y));
}

}  // namespace wlw::studio
