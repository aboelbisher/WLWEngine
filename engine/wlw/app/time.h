#pragma once

#include "wlw/core/time_scale.h"

namespace wlw::app {

class Application;

// This frame's time, as the game sees it (Application::Time()). Updated once per frame by the
// Application; read anywhere.
class Time {
 public:
  float Delta() const { return delta_; }              // game time this frame: scaled (SetScale), clamped
  float RealDelta() const { return real_delta_; }     // real time this frame, clamped (fades, debug UI)
  float FixedStep() const { return fixed_step_; }     // OnFixedUpdate's dt
  float Alpha() const { return alpha_; }              // progress towards the next fixed step (OnRender)
  double SinceStart() const { return since_start_; }  // real seconds since the app started

  // Game time speed: hit-stop (SetScale(0, 0.06f)), slow motion (SetScale(0.4f, 0.3f, 0.2f)),
  // pause (SetScale(0)). It scales OnFixedUpdate / OnUpdate and the screens' updates; fades, the
  // debug UI, audio and input stay in real time. See core::TimeScale.
  void SetScale(float scale, float hold_seconds = -1.0f, float recover_seconds = 0.0f) {
    scale_.Set(scale, hold_seconds, recover_seconds);
  }
  float Scale() const { return scale_.Scale(); }

  // Rolling averages, refreshed twice a second.
  float Fps() const { return fps_; }
  float FrameMs() const { return frame_ms_; }

 private:
  friend class Application;
  float delta_ = 0.0f;
  float real_delta_ = 0.0f;
  float fixed_step_ = 1.0f / 60.0f;
  float alpha_ = 0.0f;
  double since_start_ = 0.0;
  float fps_ = 0.0f;
  float frame_ms_ = 0.0f;
  core::TimeScale scale_;
};

}  // namespace wlw::app
