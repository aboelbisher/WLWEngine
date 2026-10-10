#pragma once

#include <memory>
#include <string>

#include "wlw/core/fixed_timestep.h"
#include "wlw/core/time_scale.h"
#include "wlw/core/vector2.h"
#include "wlw/input/input.h"
#include "wlw/scene/window.h"
#include "wlw/app/screen.h"
#include "wlw/wlw.h"

namespace wlw {

namespace audio {
class Audio;
}  // namespace audio
namespace ui {
class Canvas;
class UIRenderer;
}  // namespace ui
namespace debug {
class DebugUI;
}

struct AppConfig {
  std::string title = "WLWengine";
  core::Vector2 window_size = {1280, 720};
  float fixed_timestep = 1.0f / 60.0f;  // simulation rate: 60 steps per second
  float max_frame_time = 0.25f;         // longer frames are clamped (see FixedTimestep)
  bool show_stats_in_title = true;      // "title | 60 fps | 16.7 ms" (desktop)
  // The UI's virtual resolution: the Canvas is scaled so this area fits on any screen.
  core::Vector2 ui_reference_size = {640, 360};
};

// Base class for a game. It owns the engine, the window and the main loop; a game overrides
// the On*() hooks and/or pushes Screens (game states). One frame of Run() looks like this
// (each hook is followed by the same call on the screens: top only for updates, all visible
// screens for drawing):
//
//   measure frame time; Back -> screens, OnBack(), else leave
//   OnFixedUpdate(step)  x N   <- game logic / physics, always with the same dt
//   OnUpdate(frame_time)       <- per-frame work: input, camera
//   OnRender(alpha)            <- push the latest state to the scene, interpolated by alpha
//   OnRenderUI(canvas)         <- 2D UI over the scene
//   OnDebugUI()                <- debug panels (WLW_DEBUG_UI builds, while the debug UI is open)
//   draw + present, then poll input for the next frame; apply screen changes
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
  // Called 0..N times per frame with a constant dt (AppConfig::fixed_timestep). Game time: runs
  // slower or not at all under SetTimeScale.
  virtual void OnFixedUpdate(float dt) {}
  // Called once per frame with the (clamped) frame time, in game time (see SetTimeScale).
  virtual void OnUpdate(float dt) {}
  // Called once per frame before drawing. alpha in [0, 1) = progress towards the next fixed step.
  virtual void OnRender(float alpha) {}
  // Called once per frame after OnRender: draw the 2D UI (HUD, menus) on the canvas. It's drawn
  // over the 3D scene, in Canvas units (see ui::Canvas).
  virtual void OnRenderUI(ui::Canvas& canvas) {}
  // Called once after the loop ends, while the engine is still alive.
  virtual void OnShutdown() {}
  // Back (Android) / Escape (desktop) that no screen handled. Return true if handled; otherwise
  // the app goes to the background on Android and quits on desktop.
  virtual bool OnBack() { return false; }
  // The app went to the background (mobile). Screens get Screen::OnAppPaused too.
  virtual void OnAppPaused() {}
  // Build debug panels here with the Dear ImGui API (ImGui::Begin, SliderFloat, Button...).
  // Called once per frame after OnRender while the debug UI is open (F1 or its "Debug" button).
  // Only called in builds with WLW_DEBUG_UI=1: wrap overrides in `#if WLW_DEBUG_UI`.
  virtual void OnDebugUI() {}

  WLWEngine& Engine() { return *engine_; }
  input::Input& Input() { return window_->GetInput(); }  // keys, pointer/touch, swipes, taps
  scene::Window& Window() { return *window_; }
  // Game states (menu, play, pause...): push Screens here; the engine updates the top one and
  // draws the visible ones, after the Application's own On* hooks. Optional.
  app::ScreenStack& Screens() { return screens_; }
  // Sound output (see audio::Audio). Silent, but usable, if the device has no audio output.
  audio::Audio& Audio() { return *audio_; }
#if WLW_DEBUG_UI
  debug::DebugUI& DebugUi() { return *debug_ui_; }  // e.g. HideFor() before a screenshot
#endif
  const std::shared_ptr<scene::Window>& WindowPtr() const { return window_; }
  const AppConfig& Config() const { return config_; }

  double TimeSinceStart() const { return time_since_start_; }  // seconds, real time

  // Game time speed: hit-stop (SetTimeScale(0, 0.06f)), slow motion (SetTimeScale(0.4f, 0.3f, 0.2f)),
  // pause (SetTimeScale(0)). It scales the dt of OnFixedUpdate, OnUpdate and the screens' (Fixed)Update;
  // screen fades, the debug UI, audio and input stay in real time. See core::TimeScale.
  void SetTimeScale(float scale, float hold_seconds = -1.0f, float recover_seconds = 0.0f) {
    time_scale_.Set(scale, hold_seconds, recover_seconds);
  }
  float TimeScale() const { return time_scale_.Scale(); }
  float Fps() const { return fps_; }
  float FrameMs() const { return frame_ms_; }

 private:
  void UpdateStats(double frame_seconds);
  void BuildUI();  // the 2D UI for this frame: canvas setup, OnRenderUI, screens, fades

  AppConfig config_;
  // Destroyed in reverse order: the window handle first, the engine (GL + platform shutdown) last.
  std::unique_ptr<WLWEngine> engine_;
  std::shared_ptr<scene::Window> window_;
  std::unique_ptr<audio::Audio> audio_;
  std::unique_ptr<ui::Canvas> canvas_;
  std::unique_ptr<ui::UIRenderer> ui_renderer_;  // GL objects: destroyed before the window/engine
  int ui_overlay_id_ = -1;
  app::ScreenStack screens_;  // emptied at the end of Run(), while the game's members still exist
#if WLW_DEBUG_UI
  void DrawEngineDebugWindow();  // always shown while the debug UI is open: stats, ImGui demo
  std::unique_ptr<debug::DebugUI> debug_ui_;  // destroyed before the window and the engine
  bool show_imgui_demo_ = false;
#endif
  core::FixedTimestep timestep_;
  core::TimeScale time_scale_;

  bool quit_requested_ = false;
  double time_since_start_ = 0.0;

  // Rolling stats, refreshed twice a second.
  int stats_frames_ = 0;
  double stats_seconds_ = 0.0;
  float fps_ = 0.0f;
  float frame_ms_ = 0.0f;
};

}  // namespace wlw
