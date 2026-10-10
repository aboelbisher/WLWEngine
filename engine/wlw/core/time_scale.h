#pragma once

#include <algorithm>

namespace wlw::core {

// Game time running at a different speed from real time: hit-stop (a freeze of a few frames that
// makes an impact feel heavy), slow motion for a big moment, or a pause.
//
//   Set(0.0f, 0.06f)          freeze for 60 ms (real time), then normal speed
//   Set(0.4f, 0.3f, 0.2f)     40% speed for 0.3 s, then easing back to normal over 0.2 s
//   Set(0.5f)                 half speed until told otherwise
//
// Apply() turns each frame's real dt into game dt. A frame that crosses the end of the hold is
// split: the part inside it is scaled, the rest isn't, so the timing doesn't depend on frame rate.
class TimeScale {
 public:
  // `hold_seconds` < 0: keep the scale until the next Set. `recover_seconds`: after the hold, ease
  // linearly back to 1 over this long (0 = jump back).
  void Set(float scale, float hold_seconds = -1.0f, float recover_seconds = 0.0f) {
    scale_ = std::max(scale, 0.0f);
    hold_left_ = hold_seconds;
    recover_seconds_ = std::max(recover_seconds, 0.0f);
    recover_left_ = 0.0f;
    held_forever_ = hold_seconds < 0.0f;
    if (hold_seconds == 0.0f) {  // no hold: straight into the recovery (or back to normal)
      if (recover_seconds_ > 0.0f) {
        recover_left_ = recover_seconds_;
      } else {
        scale_ = 1.0f;
      }
    }
  }
  void Reset() { Set(1.0f); }

  // The scale right now (1 = normal).
  float Scale() const {
    if (recover_left_ > 0.0f && recover_seconds_ > 0.0f) {
      return scale_ + (1.0f - scale_) * (1.0f - recover_left_ / recover_seconds_);
    }
    return scale_;
  }
  bool Normal() const { return Scale() == 1.0f; }

  // Advances by `real_dt` seconds of real time; returns the game time that passed.
  float Apply(float real_dt) {
    float game_dt = 0.0f;
    float left = std::max(real_dt, 0.0f);
    // 1. The hold: scaled.
    if (!held_forever_ && hold_left_ > 0.0f) {
      const float part = std::min(left, hold_left_);
      game_dt += part * scale_;
      hold_left_ -= part;
      left -= part;
      if (hold_left_ <= 0.0f) {
        if (recover_seconds_ > 0.0f) {
          recover_left_ = recover_seconds_;
        } else {
          scale_ = 1.0f;
        }
      }
    } else if (held_forever_) {
      return left * scale_;
    }
    // 2. The recovery: the scale rises linearly to 1; the average over the part is exact.
    if (left > 0.0f && recover_left_ > 0.0f) {
      const float part = std::min(left, recover_left_);
      const float from = Scale();
      recover_left_ -= part;
      if (recover_left_ <= 0.0f) {  // the ramp is done: normal speed (before reading `to`)
        recover_left_ = 0.0f;
        scale_ = 1.0f;
      }
      const float to = Scale();
      game_dt += part * (from + to) * 0.5f;
      left -= part;
    }
    // 3. Normal speed for the rest of the frame.
    return game_dt + left * (hold_left_ > 0.0f || recover_left_ > 0.0f ? 0.0f : 1.0f) * Scale();
  }

 private:
  float scale_ = 1.0f;
  float hold_left_ = 0.0f;
  float recover_seconds_ = 0.0f;
  float recover_left_ = 0.0f;
  bool held_forever_ = false;
};

}  // namespace wlw::core
