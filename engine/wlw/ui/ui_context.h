#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "wlw/platform/platform.h"
#include "wlw/ui/canvas.h"

// The UI layer for one frame, on top of the Canvas (which only draws). Like Dear ImGui's context
// next to its draw list: what the UI needs besides drawing - the pointer, the safe area, and what
// widgets remember between frames.
namespace wlw::ui {

// The pointer as UI widgets see it this frame, in Canvas units (Application fills it in).
struct PointerState {
  glm::vec2 position{0.0f};
  bool down = false;      // held
  bool pressed = false;   // went down this frame
  bool released = false;  // went up this frame
};

// Application owns one for the screen and hands it to OnRenderUI / Screen::RenderUI; the widgets
// (widgets.h) take it. Draw with `canvas`; lay out inside `safe_area`.
struct UIContext {
  explicit UIContext(Canvas& canvas_to_draw_on) : canvas(canvas_to_draw_on) {}

  Canvas& canvas;            // where the UI draws: the screen's canvas
  PointerState pointer;      // this frame
  Rect safe_area;            // the part of the screen not under bars / cutouts, in canvas units
  bool interactive = true;   // false: widgets draw but don't respond (screens not on top, fades)
  uint64_t active_widget = 0;  // the widget the pointer went down on (0 = none); kept across frames
};

// The safe area in `canvas` units: its whole area minus the insets (pixels, from
// platform::GetSafeAreaInsets()).
inline Rect SafeAreaFor(const Canvas& canvas, const platform::SafeAreaInsets& insets_pixels) {
  return {canvas.FromPixels({insets_pixels.left, insets_pixels.top}),
          canvas.Size() - canvas.FromPixels({insets_pixels.right, insets_pixels.bottom})};
}

}  // namespace wlw::ui
