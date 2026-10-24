#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "wlw/app/quality.h"
#include "wlw/app/screen.h"
#include "wlw/app/system.h"
#include "wlw/app/time.h"
#include "wlw/audio/audio_config.h"
#include "wlw/core/connection.h"
#include "wlw/core/console.h"
#include "wlw/core/cvar.h"
#include "wlw/core/fixed_timestep.h"
#include "wlw/core/type_name.h"
#include "wlw/core/math.h"
#include "wlw/input/input.h"
#include "wlw/scene/window.h"

namespace wlw {

namespace audio {
class Audio;
}  // namespace audio
namespace ui {
struct UIContext;
}  // namespace ui
namespace debug {
class DebugUI;
}
namespace rendering {
class RenderDevice;
class RenderingDriver;
}  // namespace rendering

namespace app {

// ---- How the app starts (AppConfig and its groups) ----
struct WindowConfig {
  std::string title;           // the title bar's text (desktop); empty = AppConfig::name
  Vec2 size = {1280, 720};     // desktop window size in pixels (phones: always full screen)
  bool stats_in_title = true;  // "title | 60 fps | 16.7 ms" (desktop)
};

struct GraphicsConfig {
  // Frames per second the screen gets (core/frame_pacing.h): 30/60/90/120..., core::kTargetDisplayRate
  // (0) = the display's own rate, core::kTargetUncapped (-1) = no limit (desktop profiling). Phones
  // can't go past the display's rate. Change it later with Application::SetTargetFps.
  int target_fps = 60;
  // false: the screen surface gets no depth buffer (scene::WindowOptions::depth): saves its memory
  // for a game whose screen always has post passes or a render scale below 1 (else it costs a copy).
  bool screen_depth = true;
};

using AudioConfig = audio::AudioConfig;  // enabled, max_voices (audio/audio.h)

struct TimeConfig {
  float fixed_step = 1.0f / 60.0f;  // simulation rate (OnFixedUpdate): 60 steps per second
  float max_frame_time = 0.25f;     // longer frames are clamped (see FixedTimestep)
};

struct UIConfig {
  // The UI's virtual resolution: the Canvas is scaled so this area fits on any screen.
  Vec2 reference_size = {640, 360};
};

struct AppConfig {
  // The game's name: its save folder (quality.txt, cvars.cfg, the debug log; desktop and Android)
  // and the default window title. Changing it moves the saves.
  std::string name = "WLWengine";
  WindowConfig window;
  GraphicsConfig graphics;
  AudioConfig audio;
  TimeConfig time;
  UIConfig ui;
};

// The config the app runs with: the defaults that follow other fields filled in (an empty window
// title = the name). The Application calls it; it's a plain function for tests.
AppConfig ResolveAppConfig(AppConfig config);

// The app: owns the services (engine and renderer, window, input, audio, screens, time) and runs
// the main loop. Games don't subclass it: they add Systems (always running) and push Screens (one
// state at a time). A game's main.cpp:
//
//   std::unique_ptr<wlw::app::Application> MakeApp() {
//     auto app = std::make_unique<wlw::app::Application>(MakeConfig());
//     app->AddSystem<MyGame>();
//     return app;
//   }
//   WLW_MAIN(MakeApp)
//
// One frame (systems in the order added, then the screens: top only for updates, every visible
// one for drawing):
//
//    1. lifecycle       paused?  -> systems.OnAppPaused, screens   (audio suspends)
//                       resumed? -> systems.OnAppResumed          (audio resumes, clock restarts)
//    2. time            measure, clamp, apply the time scale
//    3. back            screens (top first) -> systems (last added first) -> else leave
//    4. platform        native-bridge messages (ads...) on the game thread; a call / alarm ->
//                       audio silent + screens' OnAppPaused; the player's own music -> ours mutes
//    5. OnFixedUpdate   x N, constant dt                <- game logic / physics
//    6. OnUpdate        per frame, game time            <- input, camera
//    7. OnRender        alpha in [0, 1)                 <- push the state to the scene, interpolated
//    8. OnRenderUI      canvases begun by the engine    <- 2D UI over the scene, then screen fades
//    9. OnDebugUI       WLW_DEBUG_UI builds, while open <- ImGui panels (the engine's first)
//   10. draw + present, then poll input; apply queued screen changes; Quality() reads the frame's
//       times (its OnChange runs here, so a new level shows from the next frame; so does its
//       render scale, QualitySettings::render_scales, set on the screen just before drawing)
class Application final {
 public:
  explicit Application(AppConfig config = {});
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // Adds a system; they run in the order added. Started (OnStart) when Run begins, or at the
  // next frame when added while running. Returns it, e.g. to wire systems together.
  template <typename T, typename... Args>
  T& AddSystem(Args&&... args) {
    return static_cast<T&>(
        systems_.Add(std::make_unique<T>(std::forward<Args>(args)...), this, std::string(core::TypeName<T>())));
  }
  // The first system of type T, else null (a short search: for setup, not per frame).
  template <typename T>
  T* GetSystem() const {
    return systems_.Get<T>();
  }

  // Runs until Quit() is called or the window is closed. Returns the process exit code.
  int Run();
  void Quit() { quit_requested_ = true; }

  // ---- Frame pacing ----
  // The frame-rate target (see AppConfig::graphics.target_fps), e.g. from a settings menu: 30 saves battery.
  // The same as the cvar r.target_fps. Quality's budget follows the rate frames are paced to.
  void SetTargetFps(int fps);
  int TargetFps() const;

  // ---- Services ----
  scene::Window& Window() { return *window_; }
  input::Input& Input() { return window_->GetInput(); }  // keys, pointer/touch, swipes, taps
  audio::Audio& Audio() { return *audio_; }              // silent, but usable, without an audio device
  ScreenStack& Screens() { return screens_; }            // game states (menu, play, pause...)
  app::Time& Time() { return time_; }                    // this frame's time; the time scale
  app::Quality& Quality() { return quality_; }           // quality levels from the device's speed
  const AppConfig& Config() const { return config_; }
#if WLW_DEBUG_UI
  debug::DebugUI& DebugUi() { return *debug_ui_; }  // e.g. HideFor() before a screenshot
#endif

 private:
  void BuildUI();  // phase 8: canvases, pointer, safe area, OnRenderUI, screens, fades
  void UpdateStats(double frame_seconds);
  void PlotFrame(double frame_seconds);
  void UpdateQuality(double frame_seconds);
  void ApplyQualityRenderScale();  // the level's render scale (QualitySettings::render_scales) -> the screen
  // Cvars (core/cvar.h): wlw.cfg, the player's saved ones, the command line; then the engine's own wired up.
  void ApplyStartupCVars();
  void WatchCVar(CVarBase& cvar, std::function<void()> apply);
  void SaveCVars();

  AppConfig config_;
  // Destroyed in reverse order: screens and systems first (they may hold GPU resources and
  // references to each other), then the UI and audio, the window handle, the renderer (it keeps the
  // window: releases its scenes and its own GL objects, then shuts the platform down) and the mesh
  // device last (the renderer uses it until then).
  std::unique_ptr<rendering::RenderDevice> render_device_;
  std::unique_ptr<rendering::RenderingDriver> renderer_;
  std::shared_ptr<scene::Window> window_;
  std::unique_ptr<audio::Audio> audio_;
  std::unique_ptr<ui::UIContext> ui_;  // the UI layer over the screen's canvas (pointer, safe area, widget state)
#if WLW_DEBUG_UI
  std::unique_ptr<debug::DebugUI> debug_ui_;
#endif
  SystemList systems_;
  ScreenStack screens_;  // emptied at the end of Run(), while the systems are still alive
  core::FixedTimestep timestep_;
  app::Time time_;
  app::Quality quality_;
  bool quality_timing_ = false;        // GPU timing turned on for it (once: the debug panel may turn it off)
  float quality_render_scale_ = 0.0f;  // the last one applied to the screen (0: none yet)
  bool quit_requested_ = false;
  std::string cvar_file_;                               // <save dir>/cvars.cfg: the player's Saved cvars
  std::vector<core::ScopedConnection> cvar_callbacks_;  // removed in the destructor
  std::unique_ptr<ConsoleCommand> quit_command_;

  // Stats accumulators, folded into time_ twice a second.
  int stats_frames_ = 0;
  double stats_seconds_ = 0.0;
};

}  // namespace app
}  // namespace wlw
