#pragma once

#include <memory>
#include <string>

#include "wlw/core/fixed_timestep.h"
#include "wlw/core/vector2.h"
#include "wlw/input/input.h"
#include "wlw/scene/window.h"
#include "wlw/wlw.h"

namespace wlw {

struct AppConfig {
  std::string title = "WLWengine";
  core::Vector2 window_size = {1280, 720};
  float fixed_timestep = 1.0f / 60.0f;  // simulation rate: 60 steps per second
  float max_frame_time = 0.25f;         // longer frames are clamped (see FixedTimestep)
  bool show_stats_in_title = true;      // "title | 60 fps | 16.7 ms" (desktop)
};

// Base class for a game. It owns the engine, the window and the main loop; a game overrides
// the On*() hooks. One frame of Run() looks like this:
//
//   measure frame time
//   OnFixedUpdate(step)  x N   <- game logic / physics, always with the same dt
//   OnUpdate(frame_time)       <- per-frame work: input, camera, UI
//   OnRender(alpha)            <- push the latest state to the scene, interpolated by alpha
//   draw + present, then poll input for the next frame
class Application {
 public:
  explicit Application(AppConfig config = {});
  virtual ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // Runs until Quit() is called or the window is closed. Returns the process exit code.
  int Run();
  void Quit() { quit_requested_ = true; }

 protected:
  // Called once, after the window and the renderer exist. Build your scene here.
  virtual void OnStart() {}
  // Called 0..N times per frame with a constant dt (AppConfig::fixed_timestep).
  virtual void OnFixedUpdate(float dt) {}
  // Called once per frame with the real (clamped) frame time.
  virtual void OnUpdate(float dt) {}
  // Called once per frame before drawing. alpha in [0, 1) = progress towards the next fixed step.
  virtual void OnRender(float alpha) {}
  // Called once after the loop ends, while the engine is still alive.
  virtual void OnShutdown() {}

  WLWEngine& Engine() { return *engine_; }
  input::Input& Input() { return window_->GetInput(); }  // keys, pointer/touch, swipes, taps
  scene::Window& Window() { return *window_; }
  const std::shared_ptr<scene::Window>& WindowPtr() const { return window_; }
  const AppConfig& Config() const { return config_; }

  double TimeSinceStart() const { return time_since_start_; }  // seconds
  float Fps() const { return fps_; }
  float FrameMs() const { return frame_ms_; }

 private:
  void UpdateStats(double frame_seconds);

  AppConfig config_;
  // Destroyed in reverse order: the window handle first, the engine (GL + platform shutdown) last.
  std::unique_ptr<WLWEngine> engine_;
  std::shared_ptr<scene::Window> window_;
  core::FixedTimestep timestep_;

  bool quit_requested_ = false;
  double time_since_start_ = 0.0;

  // Rolling stats, refreshed twice a second.
  int stats_frames_ = 0;
  double stats_seconds_ = 0.0;
  float fps_ = 0.0f;
  float frame_ms_ = 0.0f;
};

}  // namespace wlw
