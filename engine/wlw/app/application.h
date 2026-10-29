#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "wlw/app/frame_clock.h"
#include "wlw/app/quality.h"
#include "wlw/app/screen.h"
#include "wlw/app/system.h"
#include "wlw/app/time.h"
#include "wlw/audio/audio_config.h"
#include "wlw/core/connection.h"
#include "wlw/core/console.h"
#include "wlw/core/cvar.h"
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
namespace scene {
class OffscreenWindow;
}  // namespace scene

namespace app {

// ---- How the app starts (AppConfig and its groups) ----
struct WindowConfig {
  std::string title;           // the title bar's text (desktop); empty = AppConfig::name
  Vec2 size = {1280, 720};     // desktop window size in pixels (phones: always full screen)
  bool stats_in_title = true;  // "title | 60 fps | 16.7 ms" (desktop)
  bool visible = true;         // false: a hidden window (desktop: tests, tools); phones always show theirs
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
//
// Who drives the frames. Run() = Internal().Init(), then Internal().Frame() until it returns false,
// then Internal().Shutdown(). Whoever owns the platform's loop makes those calls instead of Run()
// where looping inside a call isn't allowed. All three come from one thread, the game thread: the
// one that called Init().
//   Desktop, Android  WLW_MAIN calls Run() (Android: on android_main's thread). The window may wait
//                     inside a frame: Init() for Android's native window, the event poll while in
//                     the background.
//   iOS (later)       either a game thread calls Run() like Android, or the app delegate calls Init()
//                     and CADisplayLink calls Frame() once per refresh (nothing waits in the frame:
//                     the display link paces it). Going to the background: one more Frame() so
//                     OnAppPaused runs before the app is suspended.
//   Web (later)       emscripten_set_main_loop calls Frame(); the Application lives on the heap (the
//                     browser unwinds main), and Shutdown() may never come.
//   The Studio        is an Application itself (it owns the OS window, the renderer and ImGui). Each Play makes
//                     the game's Application with its MakeApp() and starts it hosted on the Studio
//                     (Internal().InitHosted) on a scene::OffscreenWindow: the game's screen in a texture, at
//                     a device's size, with its own input. The Studio calls the session's Frame() from one of
//                     its own hooks (inside its ImGui frame, so the game's OnDebugUI panels draw into it);
//                     Pause / Step = Internal().SetPaused / Step; Quit() or Escape with nothing to close ends
//                     the session (its Frame() returns false), not the Studio; Stop destroys it. Like Unreal's
//                     Play In Editor (a fresh game instance per session in the editor's process).
// So save in OnAppPaused, not only in OnShutdown: iOS ends suspended apps and browsers close tabs
// without a Shutdown() (Android already can).
class Application final {
 public:
  explicit Application(AppConfig config = {});
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // Adds a system; they run in the order added. Started (OnStart) when the app starts (Run), or at
  // the next frame when added while running. Returns it, e.g. to wire systems together.
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

  // Starts, runs frames until Quit() is called or the window is closed, shuts down. Returns the
  // process exit code: Quit's, 0 when the window was closed, 1 when the app couldn't start.
  int Run();
  // Ends the app after this frame (it still finishes and is shown), like Godot's quit(code) and
  // Unity's Application.Quit(code). The last call's code wins. In the Studio it ends the play session.
  void Quit(int exit_code = 0) {
    quit_requested_ = true;
    exit_code_ = exit_code;
  }

  class InternalAccess;
  // Init / Frame / Shutdown for entry points, the editor and tests (see "Who drives the frames").
  InternalAccess Internal();

  // ---- Frame pacing ----
  // The frame-rate target (see AppConfig::graphics.target_fps), e.g. from a settings menu: 30 saves battery.
  // The same as the cvar r.target_fps. Quality's budget follows the rate frames are paced to.
  void SetTargetFps(int fps);
  int TargetFps() const;

  // ---- Services ----
  // The game's window: the OS window, or in the Studio the OffscreenWindow its session runs on (its size,
  // input and safe area are the simulated device's).
  scene::Window& Window() { return *window_; }
  input::Input& Input() { return window_->GetInput(); }  // keys, pointer/touch, swipes, taps
  audio::Audio& Audio() { return *audio_; }              // silent, but usable, without an audio device
  ScreenStack& Screens() { return screens_; }            // game states (menu, play, pause...)
  app::Time& Time() { return time_; }                    // this frame's time; the time scale
  app::Quality& Quality() { return quality_; }           // quality levels from the device's speed
  const AppConfig& Config() const { return config_; }
#if WLW_DEBUG_UI
  debug::DebugUI& DebugUi() { return *debug_ui_; }  // e.g. HideFor() before a screenshot (in the Studio: its own)
#endif

 private:
  enum class State { NotStarted, Running, Stopped };

  bool Init();
  bool InitHosted(Application& host, std::shared_ptr<scene::OffscreenWindow> window);
  bool Frame();
  int Shutdown();
  bool OnGameThread() const;
  double ClockSeconds() const;  // the monotonic clock, in seconds since the Application was made
  void BuildUI();               // phase 8: canvases, pointer, safe area, OnRenderUI, screens, fades
  void UpdateStats(double frame_seconds);
  void PlotFrame(double frame_seconds);
  void UpdateQuality(double frame_seconds);
  void ApplyQualityRenderScale();  // the level's render scale (QualitySettings::render_scales) -> the screen
  void SetPaused(bool paused);
  void UpdateAudioSuspended();  // silent while in the background or paused
  void WatchOtherAudio();       // the bridge's "audio" events (calls, other music)
  // Cvars (core/cvar.h): wlw.cfg, the player's saved ones, the command line; then the engine's own wired up.
  void ApplyStartupCVars();
  void WatchCVar(CVarBase& cvar, std::function<void()> apply);
  void SaveCVars();

  AppConfig config_;
  // Destroyed in reverse order: screens and systems first (they may hold GPU resources and
  // references to each other), then the UI and audio, the window handle, the renderer (it keeps the
  // window: releases its scenes and its own GL objects, then shuts the platform down) and the mesh
  // device last (the renderer uses it until then).
  // A hosted session (InitHosted) leaves its own device and renderer unstarted and draws with its host's.
  std::unique_ptr<rendering::RenderDevice> render_device_;
  std::unique_ptr<rendering::RenderingDriver> own_renderer_;
  rendering::RenderingDriver* renderer_ = nullptr;  // own_renderer_, or the host's
  std::shared_ptr<scene::Window> window_;
  std::unique_ptr<audio::Audio> audio_;
  std::unique_ptr<ui::UIContext> ui_;  // the UI layer over the screen's canvas (pointer, safe area, widget state)
#if WLW_DEBUG_UI
  std::unique_ptr<debug::DebugUI> own_debug_ui_;
  debug::DebugUI* debug_ui_ = nullptr;  // own_debug_ui_, or the host's
#endif
  Application* host_ = nullptr;  // the app this one is hosted on (InitHosted), else null
  int hosted_sessions_ = 0;      // apps hosted on this one, alive (they must go first)
  SystemList systems_;
  ScreenStack screens_;  // emptied by Shutdown(), while the systems are still alive
  FrameClock clock_;
  app::Time time_;
  app::Quality quality_;
  bool quality_timing_ = false;        // GPU timing turned on for it (once: the debug panel may turn it off)
  float quality_render_scale_ = 0.0f;  // the last one applied to the screen (0: none yet)
  State state_ = State::NotStarted;
  bool in_frame_ = false;
  bool paused_ = false;          // Internal().SetPaused: frames draw, the game doesn't move
  bool step_requested_ = false;  // Internal().Step: the next frame moves one fixed step
  bool in_background_ = false;   // between the window's paused and resumed (mobile)
  bool quit_requested_ = false;
  int exit_code_ = 0;
  std::thread::id game_thread_;  // the thread that called Init()
  std::chrono::steady_clock::time_point clock_origin_ = std::chrono::steady_clock::now();
  std::string cvar_file_;                               // <save dir>/cvars.cfg: the player's Saved cvars
  std::vector<core::ScopedConnection> cvar_callbacks_;  // removed in the destructor
  std::unique_ptr<ConsoleCommand> quit_command_;

  // Stats accumulators, folded into time_ twice a second.
  int stats_frames_ = 0;
  double stats_seconds_ = 0.0;
};

// Engine-only (entry points, the editor, tests): the frame loop in three calls. Games never call these;
// a game calling Frame() from a hook would be a bug (it's refused).
class Application::InternalAccess {
 public:
  explicit InternalAccess(Application& app) : app_(app) {}

  // Everything before the first frame: startup cvars, the renderer (platform, window, GL context), audio,
  // UI, debug UI, the systems' OnStart, the shader warm-up, a quality lock; the clock starts last.
  // False when the app can't start (Run() then returns 1). Once only.
  bool Init() const { return app_.Init(); }
  // Init as a session hosted on `host` (a running app: the Studio), instead of Init(): the game runs on
  // `window` (its screen drawn into the window's texture, its input fed by the host) and draws with the
  // host's renderer and GL context. Its own: systems, screens, time, audio, UI, quality (kept at its level:
  // the host's machine isn't the device). The host's: the debug UI (the session's OnDebugUI panels draw
  // into the host's ImGui frame when its Frame() runs inside one), the bridge, the log file, the console's
  // quit, the profiler's frames. Call Frame() from the host's thread, typically from one of its hooks; the
  // session ends like any app (Quit, the window's RequestClose: Frame() returns false). The host must outlive
  // it. Cvars are process-wide (like Unreal's console variables): the session's settings reach the host too.
  bool InitHosted(Application& host, std::shared_ptr<scene::OffscreenWindow> window) const {
    return app_.InitHosted(host, std::move(window));
  }
  // One whole frame (phases 1-10). False when it's time to stop: Quit() was called or the window was
  // closed (that frame still ran and was shown); also before Init() and after Shutdown().
  bool Frame() const { return app_.Frame(); }
  // The screens' OnExit and the systems' OnShutdown (once), then the exit code. Not from a hook: use
  // Quit(). The destructor calls it for a running app; it destroys everything else as before.
  int Shutdown() const { return app_.Shutdown(); }

  // Paused: frames still poll input, run the debug UI (OnDebugUI), draw and present, but the game doesn't
  // move: no Back, bridge messages (they wait), fixed or frame updates, OnRender, OnRenderUI (the UI drawn
  // last stays), screen changes or Quality; game time stands still and the game is silent. Unpaused, it
  // goes on from where it was (no long frame). The Studio's Pause; like Unity's and Unreal's.
  void SetPaused(bool paused) const { app_.SetPaused(paused); }
  bool IsPaused() const { return app_.paused_; }
  // While paused: the next frame moves the game by exactly one fixed step (one OnFixedUpdate, OnUpdate with
  // the fixed step as dt, OnRender, OnRenderUI), then it is paused again.
  void Step() const { app_.step_requested_ = true; }

  // The renderer it draws with (a hosted session: its host's), e.g. to read an OffscreenWindow's screen back.
  rendering::RenderingDriver& Renderer() const { return *app_.renderer_; }

 private:
  Application& app_;
};

inline Application::InternalAccess Application::Internal() { return InternalAccess(*this); }

}  // namespace app
}  // namespace wlw
