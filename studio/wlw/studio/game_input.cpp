#include "wlw/studio/game_input.h"

#include <algorithm>
#include <cstdint>

namespace wlw::studio {

bool GameInputForwarder::Captured() const {
  return std::any_of(buttons_down_.begin(), buttons_down_.end(), [](bool down) { return down; });
}

void GameInputForwarder::Forward(const input::Input& from, input::Input& to, const GameInputFrame& frame) {
  const input::Input::InternalAccess game = to.Internal();

  // Keys and text, in the order typed, while the panel has the keyboard; losing it lets go of everything.
  if (frame.focused) {
    for (const input::KeyEvent& event : from.RawKeyEvents()) game.OnKey(event.key, event.down, event.layout_key);
    for (const uint32_t codepoint : from.RawText()) game.OnText(codepoint);
  } else if (had_keyboard_) {
    game.ReleaseAll();
    buttons_down_ = {};
  }
  had_keyboard_ = frame.focused;

  // Where the pointer is in the game's pixels. Locked (mouse look): an unbounded position the raw motion moves.
  if (frame.cursor_locked && !was_locked_) locked_position_ = to.PointerPosition();
  if (frame.cursor_locked != was_locked_) game.ForgetPointerPosition();  // no jump between the two kinds
  was_locked_ = frame.cursor_locked;
  if (frame.cursor_locked) locked_position_ += from.RawPointerDelta();
  const Vec2 point =
      frame.cursor_locked ? locked_position_ : ToDevicePixels(frame.rect, frame.device, from.PointerPosition());
  const bool over = frame.hovered || frame.cursor_locked;

  // Moves: a mouse over the picture or dragging from it; a finger only while it is down.
  const bool moves = frame.touch ? buttons_down_[0] : (over || Captured());
  if (moves && point != to.PointerPosition()) game.OnPointerMove(point.x, point.y);

  // Buttons: pressed over the picture, released wherever (also a press and release inside one frame). The left
  // button is also the pointer, as the OS reports it; on a touch screen it is only the finger.
  const int buttons = frame.touch ? 1 : static_cast<int>(buttons_down_.size());
  for (int i = 0; i < static_cast<int>(buttons_down_.size()); ++i) {
    const auto button = static_cast<input::MouseButton>(i);
    const bool used = i < buttons;
    if (used && over && from.RawMouseButtonPressed(button) && !buttons_down_[i]) {
      if (!frame.touch) game.OnMouseButton(button, true);
      if (i == 0) game.OnPointerDown(point.x, point.y);
      buttons_down_[i] = true;
    }
    if (buttons_down_[i] && (!used || !from.RawMouseButtonDown(button))) {
      if (!frame.touch) game.OnMouseButton(button, false);
      if (i == 0) game.OnPointerUp(point.x, point.y);
      buttons_down_[i] = false;
    }
  }

  // The wheel, over the picture (a touch screen has none).
  if (!frame.touch && over) {
    if (const Vec2 scroll = from.RawScrollDelta(); scroll.x != 0.0f || scroll.y != 0.0f) {
      game.OnScroll(scroll.x, scroll.y);
    }
  }
}

}  // namespace wlw::studio
