#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/core/math.h"

namespace wlw::input {

// Platform-neutral keys, named by where they sit on a US keyboard (D18; like Unity's Input System keys, SDL
// scancodes and Godot's physical keycodes): Key::W is the key left of E on every layout, so game controls
// stay in place on AZERTY or Dvorak. Shortcuts use the key's label on the player's layout instead
// (Input::WasShortcutPressed: Ctrl+Z is the key printed Z). Platforms map their codes onto these (GLFW keys on
// desktop, AKEYCODE_* on Android: the emulator's arrow keys arrive as D-pad keys). Back = Android's Back button
// (gesture or key); on desktop, Escape plays that role (see Application).
enum class Key : uint8_t {
  Unknown = 0,
  // clang-format off
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  // Punctuation, named by the US layout (` - = [ ] \ ; ' , . /), then the ISO keyboard's extra key left of Z
  Grave, Minus, Equal, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe, Comma, Period, Slash,
  IntlBackslash,
  Space, Enter, Escape, Tab, Backspace, Insert, Delete, Home, End, PageUp, PageDown,
  Up, Down, Left, Right,
  CapsLock, ScrollLock, NumLock, PrintScreen, Pause, Menu,
  Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
  NumpadDecimal, NumpadDivide, NumpadMultiply, NumpadSubtract, NumpadAdd, NumpadEnter, NumpadEqual,
  // Modifiers as keys (Input::HeldModifiers() = the mask). Super = Windows key / Cmd.
  LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt, LeftSuper, RightSuper,
  // clang-format on
  Back,
  Count
};

// Held modifiers as a mask. Primary = the platform's shortcut key: Ctrl on Windows and Linux, Cmd (Super) on
// macOS, so a Studio shortcut is written once (`Modifiers::Primary | Modifiers::Shift` + Key::Z = redo).
enum class Modifiers : uint8_t {
  None = 0,
  Shift = 1 << 0,
  Control = 1 << 1,
  Alt = 1 << 2,  // Option on macOS
  Super = 1 << 3,
#if defined(__APPLE__)
  Primary = Super,
#else
  Primary = Control,
#endif
};
constexpr Modifiers operator|(Modifiers a, Modifiers b) {
  return static_cast<Modifiers>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr Modifiers operator&(Modifiers a, Modifiers b) {
  return static_cast<Modifiers>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr Modifiers& operator|=(Modifiers& a, Modifiers b) { return a = a | b; }
constexpr bool HasAny(Modifiers mask, Modifiers bits) { return (mask & bits) != Modifiers::None; }

// A real mouse's buttons (the pointer API below is the left button or the first finger). Back / Forward = the
// side buttons ("X1" / "X2").
enum class MouseButton : uint8_t { Left, Right, Middle, Back, Forward, Count };

enum class Swipe : uint8_t { Up, Down, Left, Right };

// One key going down or up, in the order the OS reported them (Input::RawKeyEvents): for whoever must replay
// them in order (the debug UI feeds Dear ImGui Ctrl before Z).
struct KeyEvent {
  Key key = Key::Unknown;         // the key's position
  Key layout_key = Key::Unknown;  // the key its label names on the current layout (= key when unknown)
  bool down = false;
  Modifiers modifiers = Modifiers::None;  // held just after this event (a shortcut's modifiers when it went down)
};

// Key names: "A", "1", "F5", "-", "Space", "PageUp", "Numpad7", "LeftShift"... (unique, KeyFromName reads them
// back, ignoring case; "Unknown" for Key::Unknown and for names it doesn't know). For menus and saved bindings.
const char* KeyName(Key key);
Key KeyFromName(std::string_view name);
// The key a layout's printed character names: 'a' / 'A' -> Key::A, '1' -> Digit1, '-' -> Minus (the US
// punctuation set); Unknown for anything else (a letter of another alphabet). The platforms use it to find
// the layout's key (D18).
Key KeyFromCharacter(uint32_t character);

// How shortcuts are spelled: Windows / Linux ("Ctrl+Shift+Z") or macOS ("Shift+Cmd+Z", Apple's order).
enum class KeyLabels : uint8_t { Pc, Mac };
#if defined(__APPLE__)
inline constexpr KeyLabels kNativeKeyLabels = KeyLabels::Mac;
#else
inline constexpr KeyLabels kNativeKeyLabels = KeyLabels::Pc;
#endif
std::string ShortcutName(Key key, Modifiers modifiers, KeyLabels labels = kNativeKeyLabels);

// Per-window input state. The platform layer feeds raw events (OnKey, OnPointer*...), the game
// queries it once per frame. "Pointer" = the mouse on desktop, the first finger on touch screens,
// so pointer-based code works on both.
//
// Two kinds of query:
//   IsKeyDown(k)      - state: is it held right now?
//   WasKeyPressed(k)  - event: did it go down since last frame? (never missed: a press and
//                       release inside one frame still counts; the OS's key repeat is ignored)
//
// The pointer can be *blocked* (SetPointerBlocked) while something else owns it - e.g. the debug
// UI while you drag one of its sliders. Blocked, the pointer queries (and the mouse buttons, wheel and
// movement) report nothing happening, so the game doesn't also react to that drag. Position and surface size
// stay readable. The keyboard is blocked the same way while a text field has it.
class Input {
 public:
  // ---- Keyboard (for games) ----
  bool IsKeyDown(Key key) const { return !keyboard_blocked_ && keys_down_[Index(key)]; }
  bool WasKeyPressed(Key key) const { return !keyboard_blocked_ && keys_pressed_[Index(key)]; }
  bool WasKeyReleased(Key key) const { return !keyboard_blocked_ && keys_released_[Index(key)]; }
  // The modifiers held now (either Shift key = Shift...); None while the keyboard is blocked.
  Modifiers HeldModifiers() const { return keyboard_blocked_ ? Modifiers::None : RawModifiers(); }
  // A shortcut: `key` (by its label on the player's layout: Ctrl+Z is the key printed Z, wherever it sits) went
  // down this frame while exactly `modifiers` were held at that moment (Primary+Z is not Primary+Shift+Z; a whole
  // Ctrl+Z tapped inside one frame still counts).
  bool WasShortcutPressed(Key key, Modifiers modifiers = Modifiers::None) const;
  // Characters typed this frame (Unicode code points, in order; desktop keyboards).
  const std::vector<uint32_t>& Text() const { return keyboard_blocked_ ? kNoText : text_; }

  // ---- Pointer: the mouse's left button or the first finger ----
  bool IsPointerDown() const { return !pointer_blocked_ && pointer_down_; }
  bool WasPointerPressed() const { return !pointer_blocked_ && pointer_pressed_; }
  bool WasPointerReleased() const { return !pointer_blocked_ && pointer_released_; }
  Vec2 PointerPosition() const { return pointer_position_; }  // pixels, origin top-left
  // How far the pointer moved this frame (surface units, +y down). A finger only moves while touching (no jump
  // when the next touch lands elsewhere). With the cursor locked (Window::SetCursorLocked) it keeps growing past
  // the window's edges: mouse look.
  Vec2 PointerDelta() const { return pointer_blocked_ ? Vec2(0.0f) : pointer_delta_; }
  // The size of the area PointerPosition() is measured in, in the same units. Pass this (not
  // Window::GetSize()) to Camera3D::ScreenPointToRay: on HiDPI desktops the cursor uses window
  // units while the framebuffer is bigger.
  Vec2 SurfaceSize() const { return surface_size_; }

  // Swipes completed this frame, in order. A swipe fires as soon as the finger/mouse has moved
  // far enough (no need to lift), and keeps firing for further turns in the same drag.
  const std::vector<Swipe>& Swipes() const { return pointer_blocked_ ? kNoSwipes : swipes_; }
  // A short press-and-release that didn't turn into a swipe.
  bool WasTapped() const { return !pointer_blocked_ && tapped_; }
  // True when the pointer is a finger (no hover between touches, less precise aim).
  bool IsTouchScreen() const { return touch_screen_; }

  // ---- Mouse (a real mouse; fingers don't press these) ----
  bool IsMouseButtonDown(MouseButton button) const { return !pointer_blocked_ && buttons_down_[Index(button)]; }
  bool WasMouseButtonPressed(MouseButton button) const { return !pointer_blocked_ && buttons_pressed_[Index(button)]; }
  bool WasMouseButtonReleased(MouseButton button) const {
    return !pointer_blocked_ && buttons_released_[Index(button)];
  }
  // The wheel this frame, in notches (a trackpad gives fractions): +x = right, +y = away from the player (the
  // wheel's top rolled forward, "scroll up").
  Vec2 ScrollDelta() const { return pointer_blocked_ ? Vec2(0.0f) : scroll_delta_; }

  // ---- Ownership (for engine systems such as the debug UI) ----
  void SetPointerBlocked(bool blocked) { pointer_blocked_ = blocked; }
  bool IsPointerBlocked() const { return pointer_blocked_; }
  // The real pointer state, ignoring blocking: for whoever decides about blocking.
  bool RawPointerDown() const { return pointer_down_; }
  bool RawPointerPressed() const { return pointer_pressed_; }
  bool RawPointerReleased() const { return pointer_released_; }
  Vec2 RawPointerDelta() const { return pointer_delta_; }
  bool RawMouseButtonDown(MouseButton button) const { return buttons_down_[Index(button)]; }
  bool RawMouseButtonPressed(MouseButton button) const { return buttons_pressed_[Index(button)]; }
  bool RawMouseButtonReleased(MouseButton button) const { return buttons_released_[Index(button)]; }
  Vec2 RawScrollDelta() const { return scroll_delta_; }
  // The keyboard, like the pointer: blocked while e.g. the console has it (typing must not steer the game).
  void SetKeyboardBlocked(bool blocked) { keyboard_blocked_ = blocked; }
  bool IsKeyboardBlocked() const { return keyboard_blocked_; }
  bool RawKeyDown(Key key) const { return keys_down_[Index(key)]; }
  bool RawKeyPressed(Key key) const { return keys_pressed_[Index(key)]; }
  bool RawKeyReleased(Key key) const { return keys_released_[Index(key)]; }
  Modifiers RawModifiers() const;
  // This frame's key changes in the OS's order (no repeats).
  const std::vector<KeyEvent>& RawKeyEvents() const { return key_events_; }
  const std::vector<uint32_t>& RawText() const { return text_; }

  class InternalAccess;
  // Feeding it (the platform layer's): see InternalAccess below.
  InternalAccess Internal();

 private:
  void BeginFrame();  // clear per-frame events; called before polling the OS
  void OnKey(Key key, bool down, Key layout_key);
  void OnText(uint32_t codepoint);  // a typed character
  void OnPointerDown(float x, float y);
  void OnPointerMove(float x, float y);
  void OnPointerUp(float x, float y);
  void OnMouseButton(MouseButton button, bool down);
  void OnScroll(float x, float y);
  void ReleaseAll();
  void SetSurfaceSize(float width, float height);  // swipe distance is relative to screen size
  void SetTouchScreen(bool touch) { touch_screen_ = touch; }

  static constexpr size_t kKeyCount = static_cast<size_t>(Key::Count);
  static constexpr size_t kButtonCount = static_cast<size_t>(MouseButton::Count);
  static size_t Index(Key key) { return static_cast<size_t>(key); }
  static size_t Index(MouseButton button) { return static_cast<size_t>(button); }
  static inline const std::vector<Swipe> kNoSwipes;
  static inline const std::vector<uint32_t> kNoText;
  float SwipeThreshold() const;

  std::array<bool, kKeyCount> keys_down_{};
  std::array<bool, kKeyCount> keys_pressed_{};
  std::array<bool, kKeyCount> keys_released_{};
  std::array<Key, kKeyCount> layout_keys_{};  // per key position: the layout's key when it went down
  std::vector<KeyEvent> key_events_;

  bool pointer_down_ = false;
  bool pointer_pressed_ = false;
  bool pointer_released_ = false;
  Vec2 pointer_position_{0, 0};
  Vec2 pointer_delta_{0, 0};
  bool pointer_known_ = false;  // pointer_position_ is a real position (moves from it count as delta)

  std::array<bool, kButtonCount> buttons_down_{};
  std::array<bool, kButtonCount> buttons_pressed_{};
  std::array<bool, kButtonCount> buttons_released_{};
  Vec2 scroll_delta_{0, 0};

  Vec2 swipe_origin_{0, 0};        // where the current swipe gesture is measured from
  bool swiped_this_drag_ = false;  // a drag that swiped is not a tap
  std::vector<Swipe> swipes_;
  bool tapped_ = false;
  bool pointer_blocked_ = false;
  bool keyboard_blocked_ = false;
  std::vector<uint32_t> text_;
  bool touch_screen_ = false;

  Vec2 surface_size_{720.0f, 1280.0f};
  float surface_min_side_ = 720.0f;
};

// For the platform layer (and input tests), not games: the OS's events go in here.
class Input::InternalAccess {
 public:
  explicit InternalAccess(Input& input) : input_(input) {}

  void BeginFrame() const { input_.BeginFrame(); }  // clear per-frame events; called before polling the OS
  // A key went down or up. `layout_key` = the key its label names on the current layout (KeyFromCharacter of
  // the printed character), Unknown = the same as `key`. Repeats (down while down) are ignored.
  void OnKey(Key key, bool down, Key layout_key = Key::Unknown) const { input_.OnKey(key, down, layout_key); }
  void OnText(uint32_t codepoint) const { input_.OnText(codepoint); }  // a typed character
  void OnPointerDown(float x, float y) const { input_.OnPointerDown(x, y); }
  void OnPointerMove(float x, float y) const { input_.OnPointerMove(x, y); }
  void OnPointerUp(float x, float y) const { input_.OnPointerUp(x, y); }
  // The next move sets the position without counting as movement (the cursor was locked or unlocked).
  void ForgetPointerPosition() const { input_.pointer_known_ = false; }
  void OnMouseButton(MouseButton button, bool down) const { input_.OnMouseButton(button, down); }
  void OnScroll(float x, float y) const { input_.OnScroll(x, y); }  // notches, +x right, +y away
  // Everything held goes up (the window lost focus: the OS sends the releases elsewhere).
  void ReleaseAll() const { input_.ReleaseAll(); }
  // Swipe distance is relative to the screen's size.
  void SetSurfaceSize(float width, float height) const { input_.SetSurfaceSize(width, height); }
  void SetTouchScreen(bool touch) const { input_.SetTouchScreen(touch); }

 private:
  Input& input_;
};

inline Input::InternalAccess Input::Internal() { return InternalAccess(*this); }

}  // namespace wlw::input
