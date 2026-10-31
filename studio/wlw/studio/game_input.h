#pragma once

#include <array>

#include <glm/glm.hpp>

#include "wlw/input/input.h"
#include "wlw/studio/devices.h"

namespace wlw::studio {

// What the Game panel knows about the game's picture this frame.
struct GameInputFrame {
  GameRect rect;               // where the picture is, in the Studio window's pointer units
  glm::ivec2 device{1, 1};     // the game's screen in pixels
  bool hovered = false;        // the pointer is over the picture (no other panel in front of it)
  bool focused = false;        // the Game panel has the keyboard
  bool touch = false;          // a touch screen: the left button is the finger; no other buttons, no wheel
  bool cursor_locked = false;  // the game locked its cursor and has it: motion arrives as raw deltas
};

// Feeds the game's Input from the Studio window's, like the OS feeds a real window (Unreal's PIE viewport, Unity's
// Game view): the pointer in the game's pixels while it is over the picture, or while a button pressed there is
// held (a drag goes on outside it); buttons and the wheel over the picture (a touch screen: the left button as the
// finger, and only while it is down does it move); keys and typed text while the panel has the keyboard. When the
// panel loses it, everything held goes up, as when a window loses focus. Game thread.
class GameInputForwarder {
 public:
  // `to` is the game's Input after its BeginFrame (between its frames); `from` the Studio window's, this frame.
  void Forward(const input::Input& from, input::Input& to, const GameInputFrame& frame);
  // A button pressed over the picture is still held: the game keeps the pointer.
  bool Captured() const;
  // Forget what the game was told (a new session: a fresh Input).
  void Reset() { *this = GameInputForwarder(); }

 private:
  std::array<bool, 5> buttons_down_{};  // what the game was told is held (touch: [0] = the finger)
  bool had_keyboard_ = false;
  bool was_locked_ = false;
  Vec2 locked_position_{0.0f};  // the game's unbounded pointer while its cursor is locked
};

}  // namespace wlw::studio
