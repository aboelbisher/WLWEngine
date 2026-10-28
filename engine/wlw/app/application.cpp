#include "wlw/app/application.h"

#include "wlw/app/engine_cvars.h"
#include "wlw/core/assert.h"
#include "wlw/core/build_info.h"
#include "wlw/core/log.h"
#include "wlw/core/profile.h"
#include "wlw/audio/audio.h"
#include "wlw/platform/bridge.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/render_device.h"
#include "wlw/rendering/rendering_driver.h"
#include "wlw/scene/offscreen_window.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/ui_context.h"
#if WLW_DEBUG_UI
#include "wlw/debug/debug_ui.h"
#include "wlw/debug/cvar_panel.h"
#include "wlw/debug/engine_panel.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>

namespace wlw::app {

namespace {

// `hook(system)` for every system, each in a profiler zone named after it (a slow system stands out).
template <typename Hook>
void ForEachSystem(SystemList& systems, Hook&& hook) {
  systems.ForEach([&hook](System& system) {
    WLW_PROFILE_SCOPE_DYNAMIC(system.Name().c_str());
    hook(system);
  });
}

}  // namespace

AppConfig ResolveAppConfig(AppConfig config) {
  if (config.window.title.empty()) config.window.title = config.name;
  return config;
}

Application::Application(AppConfig config)
    : config_(ResolveAppConfig(std::move(config))),
      render_device_(rendering::RenderDevice::Create()),
      own_renderer_(rendering::RenderingDriver::Create(render_device_.get())),
      renderer_(own_renderer_.get()),
      window_(scene::Window::Create(config_.window.size, config_.window.title,
                                    {.visible = config_.window.visible, .depth = config_.graphics.screen_depth})),
      clock_(config_.time.fixed_step, config_.time.max_frame_time) {
  LogThreadId();     // the main thread is T0 in log records
  profile::Start();  // names this thread "Game" in the profiler; log records go to its timeline
  // Log filters from the environment (desktop): WLW_LOG="Render=Verbose,Audio=Warning".
  if (const char* filter = std::getenv("WLW_LOG"); filter != nullptr && !SetLogFilter(filter)) {
    WLW_LOG(LogApp, Warning, "WLW_LOG=\"%s\": some parts aren't category=level", filter);
  }
  time_.fixed_step_ = config_.time.fixed_step;
  cv_target_fps.Set(config_.graphics.target_fps);  // the game's choice; wlw.cfg and the command line may override it
  if (const std::string folder = platform::GetSaveDirectory(config_.name); !folder.empty()) {
    quality_.Internal().SetSaveFile(folder + "/quality.txt");
    cvar_file_ = folder + "/cvars.cfg";
  }
  screens_.Internal().SetApp(this);  // Screen::App()
#if WLW_DEBUG_UI
  AddSystem<debug::EnginePanel>();  // the engine's own debug windows, first of the debug panels
  AddSystem<debug::CVarPanel>();
#endif
}

Application::~Application() {
  if (state_ == State::Running) Shutdown();  // OnShutdown runs exactly once, whoever drove the frames
  WLW_ASSERT(hosted_sessions_ == 0, "Application: destroyed before the sessions hosted on it");
  if (host_ != nullptr) {
    --host_->hosted_sessions_;
  } else {
    profile::Stop();  // the host's, while it runs
  }
}

int Application::Run() {
  if (Init()) {
    while (Frame()) {
    }
  }
  return Shutdown();
}

bool Application::OnGameThread() const { return std::this_thread::get_id() == game_thread_; }

double Application::ClockSeconds() const {
  // steady_clock: monotonic (never jumps when the system clock changes), available on every platform.
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - clock_origin_).count();
}

bool Application::InitHosted(Application& host, std::shared_ptr<scene::OffscreenWindow> window) {
  if (!WLW_VERIFY(state_ == State::NotStarted, "Application: Init() is called once")) return false;
  if (!WLW_VERIFY(host.state_ == State::Running && host.host_ == nullptr && window != nullptr,
                  "Application: InitHosted needs a running app that isn't hosted itself, and a window")) {
    return false;
  }
  host_ = &host;
  ++host.hosted_sessions_;
  window_ = std::move(window);
  renderer_ = host.renderer_;
#if WLW_DEBUG_UI
  debug_ui_ = host.debug_ui_;
#endif
  return Init();
}

bool Application::Init() {
  if (!WLW_VERIFY(state_ == State::NotStarted, "Application: Init() is called once")) return false;
  game_thread_ = std::this_thread::get_id();
  state_ = State::Stopped;  // until it has started: a failed start never runs frames
  const bool hosted = host_ != nullptr;
  if (!hosted) {
#ifndef NDEBUG
    // Debug builds keep a log file (the run before as log.txt.prev); release builds don't write one.
    if (const std::string folder = platform::GetSaveDirectory(config_.name); !folder.empty()) {
      StartLogFile(folder + "/log.txt");
    }
#endif
    WLW_LOG(LogApp, Info, "%s", core::BuildInfoText().c_str());  // which build a log came from
    quit_command_ = std::make_unique<ConsoleCommand>("quit", "Quits the game", [this](const ConsoleArgs&) { Quit(); });
  }
  ApplyStartupCVars();
  bool started = hosted;  // a hosted session draws with its host's renderer, already started
  if (!hosted) {
    WLW_PROFILE_SCOPE("Engine start");
    started = renderer_->Initialize(window_);  // the platform, the window and its GL context
  }
  if (!started) {
    WLW_LOG(LogApp, Error, "Engine failed to start");
    exit_code_ = 1;
    return false;
  }
  audio_ = std::make_unique<audio::Audio>(config_.audio);
  if (paused_) audio_->Suspend();  // paused before it started
  if (!hosted) WatchOtherAudio();
  ui_ = std::make_unique<ui::UIContext>(window_->MainViewport().GetCanvas());
#if WLW_DEBUG_UI
  if (!hosted) {
    own_debug_ui_ = std::make_unique<debug::DebugUI>(*window_);  // needs the GL context Start() made
    debug_ui_ = own_debug_ui_.get();
  }
#endif
  {
    WLW_PROFILE_SCOPE("Systems start");
    systems_.StartPending();  // the systems added before Init, in order
  }
  // The scenes they built: compile the shader variants they need now, not during the first frames.
  renderer_->WarmUp(*window_);
  // The game has started: ReadOnly cvars keep their values from now on. A quality lock from a config
  // file or the command line applies now that OnStart configured the levels.
  CVarBase::SetStarted(true);
  if (quality_.Configured() && cv_quality_lock.Get() >= 0) quality_.Lock(cv_quality_lock.Get());

  state_ = State::Running;
  clock_.Start(ClockSeconds());  // last: startup isn't the first frame's time
  return true;
}

void Application::WatchOtherAudio() {
  // What else is playing on the device (Android: AudioWatcher.kt). A call, an alarm or the
  // assistant interrupts us: silent until it ends, and the game pauses like when it goes to the
  // background (the player has stopped playing). The player's own music: ours steps aside.
  platform::SetBridgeHandler("audio", [this](const std::string& event, const std::string&) {
    WLW_LOG(LogApp, Info, "Audio: %s", event.c_str());
    if (event == "interrupted") {
      if (!audio_->Interrupted()) screens_.Internal().AppPaused();
      audio_->SetInterrupted(true);
    } else if (event == "uninterrupted") {
      audio_->SetInterrupted(false);
    } else if (event == "other_music_started") {
      audio_->SetOtherMusicPlaying(true);
    } else if (event == "other_music_stopped") {
      audio_->SetOtherMusicPlaying(false);
    }
  });
  // Anything reported before this handler existed was dropped: ask again.
  if (platform::HasBridge()) platform::BridgeCall("audio", "refresh");
}

bool Application::Frame() {
  if (state_ != State::Running) return false;
  WLW_ASSERT(OnGameThread(), "Application: Frame() runs on the thread that called Init()");
  if (!WLW_VERIFY(!in_frame_, "Application: Frame() called from inside a frame")) return false;
  if (quit_requested_ || window_->ShouldClose()) return false;
  in_frame_ = true;
  if (systems_.Started() < systems_.Size()) {
    WLW_PROFILE_SCOPE("Systems start");
    systems_.StartPending();  // systems added during the last frame start now
  }

  // 1. Lifecycle. Paused before resumed: coming back from the background, both can arrive in
  // one frame (the loop doesn't run while the app is away), and the game must end up paused
  // but audible.
  if (window_->ConsumePaused()) {
    WLW_PROFILE_SCOPE("App paused");
    in_background_ = true;
    UpdateAudioSuspended();  // silent in the background
    ForEachSystem(systems_, [](System& system) { system.OnAppPaused(); });
    screens_.Internal().AppPaused();  // e.g. the game screen opens its pause panel
  }
  if (window_->ConsumeResumed()) {
    WLW_PROFILE_SCOPE("App resumed");
    // Back from the background (mobile): don't count the time we were away as one huge frame.
    clock_.Restart(ClockSeconds());
    quality_.Settle();  // the first frames back are slow for reasons that pass
    in_background_ = false;
    UpdateAudioSuspended();
    ForEachSystem(systems_, [](System& system) { system.OnAppResumed(); });
  }

  // 2. Time: clamped, through the time scale, cut into fixed steps (frame_clock.h). Paused: game
  // time stands still (Hold), or moves one fixed step (Step).
  const bool stepping = paused_ && step_requested_;
  step_requested_ = false;
  const bool moves = !paused_ || stepping;  // the game moves this frame
  const double now = ClockSeconds();
  const FrameClock::Tick tick =
      !paused_ ? clock_.Advance(now, time_) : (stepping ? clock_.Step(now, time_) : clock_.Hold(now, time_));
  const double frame_seconds = tick.seconds;
  const int steps = tick.steps;
  const bool hosted = host_ != nullptr;

#if WLW_DEBUG_UI
  if (!hosted) {
    // First, so the debug UI can claim the pointer before the game reads input this frame.
    WLW_PROFILE_SCOPE("Debug UI begin");
    debug_ui_->BeginFrame(time_.real_delta_);
  }
#endif

  // 3. Back: the top screen first (close a panel...), then the systems; with nothing left to
  // close, leave like any Android app does (desktop: Escape quits; the Studio: the session ends).
  const input::Input& input = window_->GetInput();
  if (moves && (input.WasKeyPressed(input::Key::Back) || input.WasKeyPressed(input::Key::Escape))) {
    if (!screens_.Internal().Back() && !systems_.Back() && !platform::MoveToBackground()) Quit();
  }

  // 4. Results from platform code (an ad closed, a cloud save arrived...) reach the game here,
  // on its own thread, before this frame's updates. Paused, they wait. (A hosted session's host does it.)
  if (moves && !hosted) {
    WLW_PROFILE_SCOPE("Bridge messages");
    platform::DispatchBridgeMessages();
  }

  // 5-7. Updates and rendering: the systems, then the screens. Paused: none (steps = 0).
  const float step = time_.FixedStep();
  for (int i = 0; i < steps; ++i) {
    WLW_PROFILE_SCOPE("FixedUpdate");
    ForEachSystem(systems_, [step](System& system) { system.OnFixedUpdate(step); });
    {
      WLW_PROFILE_SCOPE("Screens");
      screens_.Internal().FixedUpdate(step);
    }
  }
  if (moves) {
    WLW_PROFILE_SCOPE("Update");
    const float dt = time_.delta_;
    ForEachSystem(systems_, [dt](System& system) { system.OnUpdate(dt); });
    {
      WLW_PROFILE_SCOPE("Screens");
      screens_.Internal().Update(dt);
    }
  }
  if (moves) {
    WLW_PROFILE_SCOPE("Render");
    const float alpha = time_.alpha_;
    ForEachSystem(systems_, [alpha](System& system) { system.OnRender(alpha); });
    {
      WLW_PROFILE_SCOPE("Screens");
      screens_.Internal().Render(alpha);
    }
  }

  // 8. UI. Paused: the canvases keep what was drawn last.
  if (moves) BuildUI();

  // 9. Debug panels (paused too). A hosted session's go into its host's ImGui frame, if one is open.
#if WLW_DEBUG_UI
  {
    WLW_PROFILE_SCOPE("DebugUI");
    if (debug_ui_->IsOpen() && (!hosted || debug_ui_->InFrame())) {
      ForEachSystem(systems_, [](System& system) { system.OnDebugUI(); });
      screens_.Internal().DebugUI();
    }
    if (!hosted) debug_ui_->EndFrame();  // drawn on top of the scene by DrawWindow
  }
#endif

  // 10. Draw + present the window, then poll input for the next frame (last frame's events are
  // cleared first); screen changes requested this frame happen now, between frames.
  ApplyQualityRenderScale();  // a level chosen since the last frame (or at start) draws this one
  if (window_->HasSurface()) renderer_->DrawWindow(window_);
  window_->GetInput().Internal().BeginFrame();
  {
    WLW_PROFILE_SCOPE("Poll input");
    window_->ProcessEvents();
  }
  if (!hosted) {
    WLW_PROFILE_FRAME();  // presented: one frame on the profiler's timeline
  }
  if (moves) {
    WLW_PROFILE_SCOPE("Screen changes");
    screens_.Internal().EndFrame(time_.real_delta_);
  }
  UpdateStats(frame_seconds);
  if (moves && !hosted) {  // a hosted session keeps its level: the host's machine isn't the device
    WLW_PROFILE_SCOPE("Quality");
    UpdateQuality(frame_seconds);
  }
  if (ConsumeSavedCVarsChanged()) {
    WLW_PROFILE_SCOPE("Save cvars");
    SaveCVars();  // rare: a setting the player changed
  }
  if (!hosted) PlotFrame(frame_seconds);
  in_frame_ = false;
  return !quit_requested_ && !window_->ShouldClose();
}

int Application::Shutdown() {
  if (state_ != State::Running) return exit_code_;  // never started (1), or already shut down
  WLW_ASSERT(OnGameThread(), "Application: Shutdown() runs on the thread that called Init()");
  if (!WLW_VERIFY(!in_frame_, "Application: Shutdown() called from a hook: use Quit()")) return exit_code_;
  state_ = State::Stopped;
  if (host_ == nullptr) platform::SetBridgeHandler("audio", nullptr);
  WLW_PROFILE_SCOPE("Shutdown");
  screens_.Internal().Clear();  // OnExit for every screen while the systems are still fully alive
  ForEachSystem(systems_, [](System& system) { system.OnShutdown(); });
  return exit_code_;
}

void Application::BuildUI() {
  WLW_PROFILE_SCOPE("RenderUI");
  const Vec2 framebuffer = window_->GetSize();
  const glm::vec2 reference{config_.ui.reference_size.x, config_.ui.reference_size.y};
  // Offscreen viewports' canvas layers: emptied and sized to their viewport (same units as the
  // UI's when proportioned like the screen); the game draws into them during OnRenderUI.
  for (const auto& viewport : window_->Viewports()) {
    const glm::ivec2 pixels = viewport->PixelSize({framebuffer.x, framebuffer.y});
    viewport->Internal().BeginCanvases(glm::vec2(pixels), reference);
  }

  // The screen's layers; its default one is the game UI's canvas.
  window_->MainViewport().Internal().BeginCanvases({framebuffer.x, framebuffer.y}, reference);
  ui::Canvas& canvas = ui_->canvas;
  ui_->NewFrame(time_.RealDelta());
  ui_->safe_area = ui::SafeAreaFor(canvas, window_->GetSafeAreaInsets());

  // The pointer for UI widgets, in Canvas units: pointer units -> framebuffer pixels -> units.
  // Uses the normal (blockable) queries, so the debug UI still takes precedence.
  const input::Input& input = window_->GetInput();
  const Vec2 surface = input.SurfaceSize();
  ui::PointerState pointer;
  if (surface.x > 0.0f && surface.y > 0.0f) {
    const Vec2 p = input.PointerPosition();
    pointer.position = canvas.FromPixels({p.x * framebuffer.x / surface.x, p.y * framebuffer.y / surface.y});
  }
  pointer.down = input.IsPointerDown();
  pointer.pressed = input.WasPointerPressed();
  pointer.released = input.WasPointerReleased();
  ui_->pointer = pointer;

  ui::UIContext& ui = *ui_;
  ForEachSystem(systems_, [&ui](System& system) { system.OnRenderUI(ui); });
  {
    WLW_PROFILE_SCOPE("Screens");
    screens_.Internal().RenderUI(ui);
  }
  if (const float fade = screens_.FadeAmount(); fade > 0.0f) {  // screen transitions
    canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, {0.0f, 0.0f, 0.0f, fade});
  }
}

void Application::SetTargetFps(int fps) { cv_target_fps.Set(fps); }  // its OnChange tells the window

void Application::SetPaused(bool paused) {
  if (paused == paused_) return;
  paused_ = paused;
  step_requested_ = false;
  if (!paused) quality_.Settle();  // the first frames back aren't the game's usual ones
  UpdateAudioSuspended();
}

void Application::UpdateAudioSuspended() {
  if (!audio_) return;  // not started yet: Init applies it
  if (in_background_ || paused_) {
    audio_->Suspend();
  } else {
    audio_->Resume();
  }
}

int Application::TargetFps() const { return window_->TargetFps(); }

void Application::UpdateQuality(double frame_seconds) {
  if (!quality_.Configured()) return;
  // The budget is the frame time of the rate frames are paced to (Swappy or the display may change it).
  float paced_fps = window_->PacedFps();
  if (paced_fps <= 0.0f) paced_fps = window_->RefreshRate();  // uncapped: measured against the display
  if (paced_fps > 0.0f) quality_.Internal().SetTargetFrameMs(1000.0f / paced_fps);
  if (!quality_timing_) {  // it decides from the GPU's time where the GPU can measure it
    quality_timing_ = true;
    if (window_->GpuTimingSupported()) cv_gpu_timing.Set(true);
  }
  const float gpu_ms = window_->GpuTiming() ? window_->GpuFrameMs() : -1.0f;
  quality_.Internal().Update(static_cast<float>(frame_seconds), gpu_ms, static_cast<float>(frame_seconds * 1000.0));
}

void Application::ApplyQualityRenderScale() {
  // Only when it changes: in between, the game may set the screen's scale itself. (The
  // r.render_scale cvar is an override on top: SetRenderScaleOverride.)
  const float scale = quality_.RenderScale();
  if (scale <= 0.0f || scale == quality_render_scale_) return;
  quality_render_scale_ = scale;
  window_->MainViewport().SetRenderScale(scale);
}

void Application::ApplyStartupCVars() {
  // Later sources win: shipped settings, the player's, then the command line / adb.
  ApplyCVarFile("wlw.cfg", CVarSource::ConfigFile);
  if (!cvar_file_.empty()) {
    if (const auto data = platform::ReadLocalFile(cvar_file_)) {
      ApplyCVarText({reinterpret_cast<const char*>(data->data()), data->size()}, CVarSource::UserFile, cvar_file_);
    }
  }
  for (const auto& [name, value] : ParseCVarArguments(platform::LaunchArguments())) {
    const bool known = FindCVar(name) != nullptr;  // engine and game cvars exist by now: unknown = a typo, usually
    const CVarSetResult result = SetCVar(name, value, CVarSource::CommandLine);
    if (result == CVarSetResult::Ok && !known) {
      WLW_LOG(LogApp, Warning, "Command line: no cvar is called %s (kept in case one registers later)", name.c_str());
    } else if (result == CVarSetResult::Ok) {
      WLW_LOG(LogApp, Info, "Command line: %s = %s", name.c_str(), value.c_str());
    } else {
      WLW_LOG(LogApp, Warning, "Command line: %s = %s: %s", name.c_str(), value.c_str(), CVarSetResultText(result));
    }
  }

  // The engine's own cvars drive the window, the log and the quality levels.
  if (!cv_log_filter.Get().empty()) SetLogFilter(cv_log_filter.Get());
  cvar_callbacks_.emplace_back(cv_log_filter.OnChange([] {
    SetLogLevel(LogLevel::Info);  // a new filter replaces the old one
    if (!SetLogFilter(cv_log_filter.Get())) WLW_LOG(LogApp, Warning, "log.filter: some parts aren't category=level");
  }));
  WatchCVar(cv_gpu_timing, [this] { window_->SetGpuTiming(cv_gpu_timing.Get()); });
  WatchCVar(cv_invalidate_framebuffers,
            [this] { window_->SetInvalidateFramebuffers(cv_invalidate_framebuffers.Get()); });
  WatchCVar(cv_target_fps, [this] { window_->SetTargetFps(cv_target_fps.Get()); });
  WatchCVar(cv_pacing_auto_interval, [this] { window_->SetAutoSwapInterval(cv_pacing_auto_interval.Get()); });
  WatchCVar(cv_render_scale, [this] { window_->MainViewport().SetRenderScaleOverride(cv_render_scale.Get()); });
  WatchCVar(cv_hdr, [this] {
    const int hdr = cv_hdr.Get();
    window_->MainViewport().SetFormatOverride(
        hdr < 0 ? std::nullopt
                : std::optional(hdr > 0 ? rendering::TextureFormat::RGBA16F : rendering::TextureFormat::RGBA8));
  });
  WatchCVar(cv_bloom, [this] {
    const int bloom = cv_bloom.Get();
    window_->MainViewport().Post().SetBloomOverride(bloom < 0 ? std::nullopt : std::optional(bloom > 0));
  });
  cvar_callbacks_.emplace_back(cv_quality_lock.OnChange([this] {
    if (!quality_.Configured()) return;
    if (cv_quality_lock.Get() < 0) {
      quality_.Unlock();
    } else {
      quality_.Lock(cv_quality_lock.Get());
    }
  }));
}

void Application::WatchCVar(CVarBase& cvar, std::function<void()> apply) {
  apply();  // the value it has now
  cvar_callbacks_.emplace_back(cvar.OnChange(std::move(apply)));
}

void Application::SaveCVars() {
  if (cvar_file_.empty()) return;
  const std::string text = SavedCVarText();
  if (!platform::WriteFileAtomic(cvar_file_, {text.begin(), text.end()})) {
    WLW_LOG(LogApp, Warning, "Couldn't save %s", cvar_file_.c_str());
  }
}

// Profile builds: this frame's numbers as graphs under the timeline (whole numbers, see profile.h).
void Application::PlotFrame(double frame_seconds) {
  if constexpr (profile::kEnabled) {
    [[maybe_unused]] const scene::RenderStats& stats = window_->MainViewport().GetRenderStats();
    WLW_PROFILE_PLOT("Frame us", frame_seconds * 1e6);
    WLW_PROFILE_PLOT("Draw calls (screen)", stats.draw_calls);
    WLW_PROFILE_PLOT("Program changes (screen)", stats.program_changes);
    WLW_PROFILE_PLOT("Texture binds (screen)", stats.texture_binds);
    WLW_PROFILE_PLOT("Instance bytes (screen)", stats.instance_bytes);
    WLW_PROFILE_PLOT("Mixer voices", audio_->GetMixer().ActiveVoices());
    if (quality_.Configured()) WLW_PROFILE_PLOT("Quality level", quality_.Level());
    if (window_->GpuTiming()) WLW_PROFILE_PLOT("GPU us", window_->GpuFrameMs() * 1000.0f);
  } else {
    static_cast<void>(frame_seconds);
  }
}

void Application::UpdateStats(double frame_seconds) {
  ++stats_frames_;
  stats_seconds_ += frame_seconds;
  if (stats_seconds_ < 0.5) return;
  time_.fps_ = static_cast<float>(stats_frames_ / stats_seconds_);
  time_.frame_ms_ = static_cast<float>(1000.0 * stats_seconds_ / stats_frames_);
  stats_frames_ = 0;
  stats_seconds_ = 0.0;

  if (config_.window.stats_in_title) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), " | %.0f fps | %.1f ms", time_.fps_, time_.frame_ms_);
    window_->SetTitle(config_.window.title + buffer);
  }
}

}  // namespace wlw::app
