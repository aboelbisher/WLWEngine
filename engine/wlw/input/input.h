#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "wlw/core/vector2.h"

namespace wlw::input {

// Platform-neutral keys. Platforms map their key codes onto these (GLFW keys on desktop,
// AKEYCODE_* on Android - the emulator's arrow keys arrive as D-pad keys).
enum class Key : uint8_t { Unknown = 0, Up, Down, Left, Right, Space, Enter, Escape, W, A, S, D, Count };

enum class Swipe : uint8_t { Up, Down, Left, Right };

// Per-window input state. The platform layer feeds raw events (OnKey, OnPointer*), the game
// queries it once per frame. "Pointer" = the mouse on desktop, the first finger on touch screens,
// so pointer-based code works on both.
//
// Two kinds of query:
//   IsKeyDown(k)      - state: is it held right now?
//   WasKeyPressed(k)  - event: did it go down since last frame? (never missed: a press and
//                       release inside one frame still counts)
class Input {
 public:
  // ---- Queries (for games) ----
  bool IsKeyDown(Key key) const { return keys_down_[Index(key)]; }
  bool WasKeyPressed(Key key) const { return keys_pressed_[Index(key)]; }
  bool WasKeyReleased(Key key) const { return keys_released_[Index(key)]; }

  bool IsPointerDown() const { return pointer_down_; }
  bool WasPointerPressed() const { return pointer_pressed_; }
  bool WasPointerReleased() const { return pointer_released_; }
  core::Vector2 PointerPosition() const { return pointer_position_; }  // pixels, origin top-left

  // Swipes completed this frame, in order. A swipe fires as soon as the finger/mouse has moved
  // far enough (no need to lift), and keeps firing for further turns in the same drag.
  const std::vector<Swipe>& Swipes() const { return swipes_; }
  // A short press-and-release that didn't turn into a swipe.
  bool WasTapped() const { return tapped_; }

  // ---- Feeding (for the platform layer) ----
  void BeginFrame();  // clear per-frame events; called before polling the OS
  void OnKey(Key key, bool down);
  void OnPointerDown(float x, float y);
  void OnPointerMove(float x, float y);
  void OnPointerUp(float x, float y);
  void SetSurfaceSize(float width, float height);  // swipe distance is relative to screen size

 private:
  static constexpr size_t kKeyCount = static_cast<size_t>(Key::Count);
  static size_t Index(Key key) { return static_cast<size_t>(key); }
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

  float surface_min_side_ = 720.0f;
};

}  // namespace wlw::input
