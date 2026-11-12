#include "wlw/input/input.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace wlw::input {

namespace {

// KeyName's names, in Key's order.
constexpr const char* kKeyNames[] = {
    "Unknown",
    // clang-format off
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "`", "-", "=", "[", "]", "\\", ";", "'", ",", ".", "/",
    "IntlBackslash",
    "Space", "Enter", "Escape", "Tab", "Backspace", "Insert", "Delete", "Home", "End", "PageUp", "PageDown",
    "Up", "Down", "Left", "Right",
    "CapsLock", "ScrollLock", "NumLock", "PrintScreen", "Pause", "Menu",
    "Numpad0", "Numpad1", "Numpad2", "Numpad3", "Numpad4", "Numpad5", "Numpad6", "Numpad7", "Numpad8", "Numpad9",
    "NumpadDecimal", "NumpadDivide", "NumpadMultiply", "NumpadSubtract", "NumpadAdd", "NumpadEnter", "NumpadEqual",
    "LeftShift", "RightShift", "LeftControl", "RightControl", "LeftAlt", "RightAlt", "LeftSuper", "RightSuper",
    // clang-format on
    "Back",
};
static_assert(std::size(kKeyNames) == static_cast<size_t>(Key::Count), "a name per key");

// The US punctuation keys and their characters, in Key's order from Grave.
constexpr char kPunctuation[] = "`-=[]\\;',./";

bool EqualsIgnoringCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

Key Offset(Key first, uint32_t offset) { return static_cast<Key>(static_cast<uint32_t>(first) + offset); }

}  // namespace

const char* KeyName(Key key) {
  const auto index = static_cast<size_t>(key);
  return index < std::size(kKeyNames) ? kKeyNames[index] : kKeyNames[0];
}

Key KeyFromName(std::string_view name) {
  for (size_t i = 0; i < std::size(kKeyNames); ++i) {
    if (EqualsIgnoringCase(name, kKeyNames[i])) return static_cast<Key>(i);
  }
  return Key::Unknown;
}

Key KeyFromCharacter(uint32_t character) {
  if (character >= 'a' && character <= 'z') return Offset(Key::A, character - 'a');
  if (character >= 'A' && character <= 'Z') return Offset(Key::A, character - 'A');
  if (character >= '0' && character <= '9') return Offset(Key::Digit0, character - '0');
  if (character == 0 || character > 127) return Key::Unknown;
  if (const char* found = std::strchr(kPunctuation, static_cast<char>(character))) {
    return Offset(Key::Grave, static_cast<uint32_t>(found - kPunctuation));
  }
  return Key::Unknown;
}

std::string ShortcutName(Key key, Modifiers modifiers, KeyLabels labels) {
  std::string text;
  const auto add = [&](Modifiers bit, const char* name) {
    if (HasAny(modifiers, bit)) text.append(name).append("+");
  };
  if (labels == KeyLabels::Mac) {  // Apple's order: Control, Option, Shift, Command
    add(Modifiers::Control, "Ctrl");
    add(Modifiers::Alt, "Option");
    add(Modifiers::Shift, "Shift");
    add(Modifiers::Super, "Cmd");
  } else {
    add(Modifiers::Control, "Ctrl");
    add(Modifiers::Alt, "Alt");
    add(Modifiers::Shift, "Shift");
    add(Modifiers::Super, "Super");
  }
  return text.append(KeyName(key));
}

Modifiers Input::RawModifiers() const {
  Modifiers mask = Modifiers::None;
  const auto held = [this](Key a, Key b) { return keys_down_[Index(a)] || keys_down_[Index(b)]; };
  if (held(Key::LeftShift, Key::RightShift)) mask |= Modifiers::Shift;
  if (held(Key::LeftControl, Key::RightControl)) mask |= Modifiers::Control;
  if (held(Key::LeftAlt, Key::RightAlt)) mask |= Modifiers::Alt;
  if (held(Key::LeftSuper, Key::RightSuper)) mask |= Modifiers::Super;
  return mask;
}

bool Input::WasShortcutPressed(Key key, Modifiers modifiers) const {
  if (keyboard_blocked_ || key == Key::Unknown) return false;
  for (const KeyEvent& event : key_events_) {  // the modifiers as they were when the key went down
    if (event.down && event.layout_key == key && event.modifiers == modifiers) return true;
  }
  return false;
}

void Input::BeginFrame() {
  keys_pressed_.fill(false);
  keys_released_.fill(false);
  key_events_.clear();
  pointer_pressed_ = false;
  pointer_released_ = false;
  pointer_delta_ = {0, 0};
  buttons_pressed_.fill(false);
  buttons_released_.fill(false);
  scroll_delta_ = {0, 0};
  swipes_.clear();
  tapped_ = false;
  text_.clear();

  // Touches lifted last frame had their last frame; the others start this one still.
  size_t kept = 0;
  for (size_t i = 0; i < touches_.size(); ++i) {
    if (touches_[i].released) continue;
    touches_[kept] = touches_[i];
    touch_os_ids_[kept] = touch_os_ids_[i];
    touches_[kept].pressed = false;
    touches_[kept].delta = {0, 0};
    ++kept;
  }
  touches_.resize(kept);
  touch_os_ids_.resize(kept);
}

const Touch* Input::FindTouch(uint32_t id) const {
  if (pointer_blocked_) return nullptr;
  for (const Touch& touch : touches_) {
    if (touch.id == id) return &touch;
  }
  return nullptr;
}

int Input::TouchCount() const {
  if (pointer_blocked_) return 0;
  return static_cast<int>(
      std::count_if(touches_.begin(), touches_.end(), [](const Touch& touch) { return touch.IsDown(); }));
}

int Input::FindDownTouch(int64_t os_id) const {
  for (size_t i = 0; i < touches_.size(); ++i) {
    if (touch_os_ids_[i] == os_id && touches_[i].IsDown()) return static_cast<int>(i);
  }
  return -1;
}

void Input::OnTouchDown(int64_t os_id, float x, float y) {
  if (const int missed = FindDownTouch(os_id); missed >= 0) EndTouch(static_cast<size_t>(missed), true);
  const bool alone = std::none_of(touches_.begin(), touches_.end(), [](const Touch& touch) { return touch.IsDown(); });
  Touch touch;
  touch.id = next_touch_id_++;
  if (next_touch_id_ == 0) next_touch_id_ = 1;  // 0 stays "no touch"
  touch.position = touch.start_position = {x, y};
  touch.pressed = true;
  touches_.push_back(touch);
  touch_os_ids_.push_back(os_id);
  if (alone && primary_touch_ == 0) {  // a finger on an empty screen is the pointer until it lifts
    primary_touch_ = touch.id;
    OnPointerDown(x, y);
  }
}

void Input::OnTouchMove(int64_t os_id, float x, float y) {
  const int i = FindDownTouch(os_id);
  if (i < 0) return;
  Touch& touch = touches_[static_cast<size_t>(i)];
  touch.delta += Vec2(x, y) - touch.position;
  touch.position = {x, y};
  if (touch.id == primary_touch_) OnPointerMove(x, y);
}

void Input::OnTouchUp(int64_t os_id, float x, float y, bool canceled) {
  const int i = FindDownTouch(os_id);
  if (i < 0) return;
  OnTouchMove(os_id, x, y);  // a fast flick may only report its end position
  EndTouch(static_cast<size_t>(i), canceled);
}

void Input::EndTouch(size_t index, bool canceled) {
  Touch& touch = touches_[index];
  touch.released = true;
  touch.canceled = canceled;
  if (touch.id != primary_touch_) return;
  primary_touch_ = 0;
  if (!canceled) {
    OnPointerUp(touch.position.x, touch.position.y);
  } else if (pointer_down_) {  // ended by the OS: released, but not a tap
    pointer_down_ = false;
    pointer_released_ = true;
    if (touch_screen_) pointer_known_ = false;
  }
}

void Input::CancelTouches() {
  for (size_t i = 0; i < touches_.size(); ++i) {
    if (touches_[i].IsDown()) EndTouch(i, true);
  }
}

void Input::OnKey(Key key, bool down, Key layout_key) {
  if (key == Key::Unknown || key >= Key::Count) return;
  const size_t i = Index(key);
  if (down == keys_down_[i]) return;  // the OS's key repeat (or a release we never saw go down)
  if (down) {
    keys_pressed_[i] = true;
    layout_keys_[i] = layout_key == Key::Unknown || layout_key >= Key::Count ? key : layout_key;
  } else {
    keys_released_[i] = true;
  }
  keys_down_[i] = down;
  key_events_.push_back({key, layout_keys_[i], down, RawModifiers()});  // a release names the key it went down as
}

void Input::OnText(uint32_t codepoint) { text_.push_back(codepoint); }

void Input::OnPointerDown(float x, float y) {
  pointer_down_ = true;
  pointer_pressed_ = true;
  pointer_position_ = {x, y};  // a finger landing elsewhere is not movement
  pointer_known_ = true;
  swipe_origin_ = {x, y};
  swiped_this_drag_ = false;
}

void Input::OnPointerMove(float x, float y) {
  if (pointer_known_) pointer_delta_ += Vec2(x, y) - pointer_position_;
  pointer_position_ = {x, y};
  pointer_known_ = true;
  if (!pointer_down_) return;

  const float dx = x - swipe_origin_.x;
  const float dy = y - swipe_origin_.y;
  if (std::max(std::fabs(dx), std::fabs(dy)) < SwipeThreshold()) return;

  // The dominant axis decides the direction. Screen y grows downwards.
  if (std::fabs(dx) > std::fabs(dy)) {
    swipes_.push_back(dx > 0 ? Swipe::Right : Swipe::Left);
  } else {
    swipes_.push_back(dy > 0 ? Swipe::Down : Swipe::Up);
  }
  swiped_this_drag_ = true;
  swipe_origin_ = {x, y};  // re-arm: keep dragging to chain another swipe
}

void Input::OnPointerUp(float x, float y) {
  OnPointerMove(x, y);  // a fast flick may only report its end position
  if (pointer_down_ && !swiped_this_drag_) tapped_ = true;
  pointer_down_ = false;
  pointer_released_ = true;
  if (touch_screen_) pointer_known_ = false;  // the next touch may land anywhere
}

void Input::OnMouseButton(MouseButton button, bool down) {
  if (button >= MouseButton::Count) return;
  const size_t i = Index(button);
  if (down == buttons_down_[i]) return;
  (down ? buttons_pressed_ : buttons_released_)[i] = true;
  buttons_down_[i] = down;
}

void Input::OnScroll(float x, float y) { scroll_delta_ += Vec2(x, y); }

void Input::ReleaseAll() {
  for (size_t i = 1; i < kKeyCount; ++i) {
    if (keys_down_[i]) OnKey(static_cast<Key>(i), false, Key::Unknown);
  }
  for (size_t i = 0; i < kButtonCount; ++i) OnMouseButton(static_cast<MouseButton>(i), false);
  CancelTouches();
  if (pointer_down_) {  // ended, but not a tap: the player didn't lift it here
    pointer_down_ = false;
    pointer_released_ = true;
  }
}

void Input::SetSurfaceSize(float width, float height) {
  surface_size_ = {width, height};
  surface_min_side_ = std::max(1.0f, std::min(width, height));
}

float Input::SwipeThreshold() const {
  return 0.06f * surface_min_side_;  // ~6% of the short screen side: ~65 px on a 1080p phone
}

}  // namespace wlw::input
