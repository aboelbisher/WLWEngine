#include "wlw/input/input.h"

#include <algorithm>
#include <cmath>

namespace wlw::input {

void Input::BeginFrame() {
  keys_pressed_.fill(false);
  keys_released_.fill(false);
  pointer_pressed_ = false;
  pointer_released_ = false;
  swipes_.clear();
  tapped_ = false;
}

void Input::OnKey(Key key, bool down) {
  if (key == Key::Unknown) return;
  const size_t i = Index(key);
  if (down && !keys_down_[i]) keys_pressed_[i] = true;  // ignore OS key-repeat
  if (!down && keys_down_[i]) keys_released_[i] = true;
  keys_down_[i] = down;
}

void Input::OnPointerDown(float x, float y) {
  pointer_down_ = true;
  pointer_pressed_ = true;
  pointer_position_ = {x, y};
  swipe_origin_ = {x, y};
  swiped_this_drag_ = false;
}

void Input::OnPointerMove(float x, float y) {
  pointer_position_ = {x, y};
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
}

void Input::SetSurfaceSize(float width, float height) {
  surface_min_side_ = std::max(1.0f, std::min(width, height));
}

float Input::SwipeThreshold() const {
  return 0.06f * surface_min_side_;  // ~6% of the short screen side: ~65 px on a 1080p phone
}

}  // namespace wlw::input
