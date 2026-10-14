#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "wlw/platform/platform.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/theme.h"

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

  Canvas& canvas;              // where the UI draws: the screen's canvas
  PointerState pointer;        // this frame
  Rect safe_area;              // the part of the screen not under bars / cutouts, in canvas units
  bool interactive = true;     // false: widgets draw but don't respond (screens not on top, fades)
  uint64_t active_widget = 0;  // the widget the pointer went down on (0 = none); kept across frames
  Theme theme;                 // how the widgets look (the game sets it, e.g. in its System's OnRenderUI)
  float dt = 0.0f;             // real seconds since the last frame: widget animations

  // A number a widget keeps between frames (like Dear ImGui's storage): a switch's knob position, a
  // press, a dialog sliding in. `initial` the first frame it's asked for. Values not asked for
  // during a frame are dropped at the next NewFrame, so a widget shown again starts fresh.
  float& Memory(uint64_t id, float initial = 0.0f) {
    for (MemoryEntry& entry : memory_) {
      if (entry.id == id) {
        entry.used = true;
        return entry.value;
      }
    }
    memory_.push_back({id, initial, true});
    return memory_.back().value;
  }
  size_t MemorySize() const { return memory_.size(); }

  // Application calls it before each frame's UI.
  void NewFrame(float seconds) {
    dt = seconds;
    std::erase_if(memory_, [](const MemoryEntry& entry) { return !entry.used; });
    for (MemoryEntry& entry : memory_) entry.used = false;
  }

 private:
  struct MemoryEntry {
    uint64_t id;
    float value;
    bool used;
  };
  std::vector<MemoryEntry> memory_;  // a few dozen at most: a linear search beats a map
};

// The safe area in `canvas` units: its whole area minus the insets (pixels, from
// platform::GetSafeAreaInsets()).
inline Rect SafeAreaFor(const Canvas& canvas, const platform::SafeAreaInsets& insets_pixels) {
  return {canvas.FromPixels({insets_pixels.left, insets_pixels.top}),
          canvas.Size() - canvas.FromPixels({insets_pixels.right, insets_pixels.bottom})};
}

}  // namespace wlw::ui
