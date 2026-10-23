#pragma once

#include "wlw/core/color.h"
#include "wlw/ui/canvas.h"

// A small set of vector icons drawn with Canvas shapes (rects, circles, arcs, triangles, lines), so
// they're sharp at any size and take any colour, with no image files (like the code-drawn icons of
// Godot's editor, or an icon font such as Material Symbols). Solid, chunky shapes that read at
// 20 units and up. Pieces may overlap, so draw icons with an opaque colour: a translucent one shows
// the overlaps darker.
namespace wlw::ui {

enum class Icon {
  None,
  Play,
  Pause,
  Back,   // <
  Next,   // >
  Close,  // X
  Check,
  Settings,  // a gear
  Music,
  Sound,
  Vibration,
  Trophy,
  Help,  // ?
  Restart,
  Undo,
  Hint,  // a light bulb
  Star,
  Lock,
  Clock,
  Privacy,  // a shield
  Home,
  Grid,
};

// `icon` fitted into the square centred in `box`.
void DrawIcon(Canvas& canvas, Icon icon, const Rect& box, const Color& color);

}  // namespace wlw::ui
