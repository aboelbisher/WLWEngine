#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "wlw/core/vector2.h"

namespace wlw::input {

// Platform-neutral keys. Platforms map their key codes onto these (GLFW keys on desktop,
// AKEYCODE_* on Android - the emulator's arrow keys arrive as D-pad keys). Back = Android's Back
// button (gesture or key); on desktop, Escape plays that role (see Application).
enum class Key : uint8_t {
  Unknown = 0,
  Up,
  Down,
  Left,
  Right,
  Space,
  Enter,
  Escape,
  W,
  A,
  S,
  D,
  I,
  P,
  F1,
  Back,
  // Text editing (the console, the debug UI) and its toggle key (` on US layouts)
  Tab,
  Backspace,
  Delete,
  Home,
  End,
  PageUp,
  PageDown,
  Control,
  Shift,
  Grave,
  Count
};

enum class Swipe : uint8_t { Up, Down, Left, Right };

// Per-window input state. The platform layer feeds raw events (OnKey, OnPointer*), the game
// queries it once per frame. "Pointer" = the mouse on desktop, the first finger on touch screens,
// so pointer-based code works on both.
//
// Two kinds of query:
//   IsKeyDown(k)      - state: is it held right now?
//   WasKeyPressed(k)  - event: did it go down since last frame? (never missed: a press and
//                       release inside one frame still counts)
//
// The pointer can be *blocked* (SetPointerBlocked) while something else owns it - e.g. the debug
// UI while you drag one of its sliders. Blocked, the pointer queries report nothing happening, so
// the game doesn't also react to that drag. Position and surface size stay readable.
class Input {
 public:
  // ---- Queries (for games) ----
  bool IsKeyDown(Key key) const { return !keyboard_blocked_ && keys_down_[Index(key)]; }
  bool WasKeyPressed(Key key) const { return !keyboard_blocked_ && keys_pressed_[Index(key)]; }
  bool WasKeyReleased(Key key) const { return !keyboard_blocked_ && keys_released_[Index(key)]; }
  // Characters typed this frame (Unicode code points, in order; desktop keyboards).
  const std::vector<uint32_t>& Text() const { return keyboard_blocked_ ? kNoText : text_; }

  bool IsPointerDown() const { return !pointer_blocked_ && pointer_down_; }
  bool WasPointerPressed() const { return !pointer_blocked_ && pointer_pressed_; }
  bool WasPointerReleased() const { return !pointer_blocked_ && pointer_released_; }
  core::Vector2 PointerPosition() const { return pointer_position_; }  // pixels, origin top-left
  // The size of the area PointerPosition() is measured in, in the same units. Pass this (not
  // Window::GetSize()) to Camera3D::ScreenPointToRay: on HiDPI desktops the cursor uses window
  // units while the framebuffer is bigger.
  core::Vector2 SurfaceSize() const { return surface_size_; }

  // Swipes completed this frame, in order. A swipe fires as soon as the finger/mouse has moved
  // far enough (no need to lift), and keeps firing for further turns in the same drag.
  const std::vector<Swipe>& Swipes() const { return pointer_blocked_ ? kNoSwipes : swipes_; }
  // A short press-and-release that didn't turn into a swipe.
  bool WasTapped() const { return !pointer_blocked_ && tapped_; }
  // True when the pointer is a finger (no hover between touches, less precise aim).
  bool IsTouchScreen() const { return touch_screen_; }

  // ---- Ownership (for engine systems such as the debug UI) ----
  void SetPointerBlocked(bool blocked) { pointer_blocked_ = blocked; }
  bool IsPointerBlocked() const { return pointer_blocked_; }
  // The real pointer state, ignoring blocking: for whoever decides about blocking.
  bool RawPointerDown() const { return pointer_down_; }
  bool RawPointerPressed() const { return pointer_pressed_; }
  bool RawPointerReleased() const { return pointer_released_; }
  // The keyboard, like the pointer: blocked while e.g. the console has it (typing must not steer the game).
  void SetKeyboardBlocked(bool blocked) { keyboard_blocked_ = blocked; }
  bool IsKeyboardBlocked() const { return keyboard_blocked_; }
  bool RawKeyDown(Key key) const { return keys_down_[Index(key)]; }
  bool RawKeyPressed(Key key) const { return keys_pressed_[Index(key)]; }
  bool RawKeyReleased(Key key) const { return keys_released_[Index(key)]; }
  const std::vector<uint32_t>& RawText() const { return text_; }

  // ---- Feeding (for the platform layer) ----
  void BeginFrame();  // clear per-frame events; called before polling the OS
  void OnKey(Key key, bool down);
  void OnText(uint32_t codepoint);  // a typed character
  void OnPointerDown(float x, float y);
  void OnPointerMove(float x, float y);
  void OnPointerUp(float x, float y);
  void SetSurfaceSize(float width, float height);  // swipe distance is relative to screen size
  void SetTouchScreen(bool touch) { touch_screen_ = touch; }

 private:
  static constexpr size_t kKeyCount = static_cast<size_t>(Key::Count);
  static size_t Index(Key key) { return static_cast<size_t>(key); }
  static inline const std::vector<Swipe> kNoSwipes;
  static inline const std::vector<uint32_t> kNoText;
  float SwipeThreshold() const;

  std::array<bool, kKeyCount> keys_down_{};
  std::array<bool, kKeyCount> keys_pressed_{};
  std::array<bool, kKeyCount> keys_released_{};

  bool pointer_down_ = false;
  bool pointer_pressed_ = false;
  bool pointer_released_ = false;
  core::Vector2 pointer_position_{0, 0};

  core::Vector2 swipe_origin_{0, 0};  // where the current swipe gesture is measured from
  bool swiped_this_drag_ = false;     // a drag that swiped is not a tap
  std::vector<Swipe> swipes_;
  bool tapped_ = false;
  bool pointer_blocked_ = false;
  bool keyboard_blocked_ = false;
  std::vector<uint32_t> text_;
  bool touch_screen_ = false;

  core::Vector2 surface_size_{720.0f, 1280.0f};
  float surface_min_side_ = 720.0f;
};

}  // namespace wlw::input
