#pragma once

#include "wlw/core/color.h"

namespace wlw::ui {

class Font;

// How the widgets look, in one place (like Godot's Theme resource, Unity's USS variables or
// Material's colour roles): widgets ask for "the primary colour" or "a raised surface", never for
// RGB values, so a game restyles every widget by changing its Theme (UIContext::theme).
// The defaults are a dark navy UI with a gold primary.
struct Theme {
  const Font* font = nullptr;  // widgets draw nothing with text until it's set

  core::Color primary{0.98f, 0.76f, 0.25f, 1.0f};     // the main action: PLAY, OK
  core::Color on_primary{0.12f, 0.09f, 0.16f, 1.0f};  // text and icons on it
  core::Color secondary{0.29f, 0.33f, 0.50f, 1.0f};   // everything else you can press
  core::Color on_secondary{1.0f, 1.0f, 1.0f, 1.0f};
  core::Color danger{0.92f, 0.33f, 0.36f, 1.0f};  // close, quit, delete
  core::Color on_danger{1.0f, 1.0f, 1.0f, 1.0f};

  core::Color surface{0.13f, 0.15f, 0.25f, 0.98f};        // dialogs and panels
  core::Color surface_raised{0.19f, 0.22f, 0.35f, 1.0f};  // rows and tiles on a surface
  core::Color track{0.07f, 0.08f, 0.14f, 1.0f};           // wells: a switch that's off, tabs, chips
  core::Color on{0.35f, 0.82f, 0.47f, 1.0f};              // a switch that's on
  core::Color text{1.0f, 1.0f, 1.0f, 1.0f};
  core::Color text_muted{0.64f, 0.68f, 0.82f, 1.0f};
  core::Color ink{0.06f, 0.07f, 0.12f, 1.0f};     // text outlines, shadows
  core::Color scrim{0.02f, 0.03f, 0.07f, 0.65f};  // dims what's behind a dialog

  float radius = 16.0f;        // buttons, cards
  float radius_small = 10.0f;  // rows, chips, small buttons
  float radius_large = 26.0f;  // dialogs
  float depth = 5.0f;          // the darker lip under raised things; held, the face sinks onto it
  float spacing = 12.0f;       // between widgets
  float outline = 0.07f;       // text outline thickness (fraction of the text size; 0 = none)
};

}  // namespace wlw::ui
