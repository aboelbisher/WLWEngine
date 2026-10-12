#pragma once

#include <string_view>

#include "wlw/core/color.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"
#include "wlw/ui/ui_context.h"

// Immediate-mode widgets, drawn with the Canvas: call one every frame you want it shown. They take
// the UIContext for the pointer and for what they remember between frames (which one is held).
namespace wlw::ui {

// How a Button / Toggle looks.
struct ButtonStyle {
  core::Color fill{0.98f, 0.76f, 0.25f, 1.0f};
  core::Color text{0.10f, 0.08f, 0.16f, 1.0f};
  float text_size = 0.0f;  // 0 = 45% of the button height
  float corner_radius = 14.0f;
  core::Color shadow{0.0f, 0.0f, 0.0f, 0.35f};  // a darker slab under the button; alpha 0 = none
};

// A button: returns true on the frame it was clicked (pressed on it and released on it - sliding
// off cancels). It shrinks a little while held. Only responds while ui.interactive.
bool Button(UIContext& ui, const Font& font, std::string_view label, const Rect& rect, const ButtonStyle& style = {});

// An on/off switch drawn as a button reading "<label> ON" or "<label> OFF" (dimmed when off).
// Flips `value` and returns true on the frame it was clicked.
bool Toggle(UIContext& ui, const Font& font, std::string_view label, bool& value, const Rect& rect,
            const ButtonStyle& style = {});

// A rounded box with a soft shadow under it: the background of dialogs and panels (drawing only).
void Panel(Canvas& canvas, const Rect& rect, const core::Color& fill, float corner_radius = 24.0f,
           const core::Color& shadow = {0.0f, 0.0f, 0.0f, 0.35f});

}  // namespace wlw::ui
