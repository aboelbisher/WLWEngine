#include "wlw/app/application.h"

#include "wlw/app/engine_cvars.h"
#include "wlw/core/log.h"
#include "wlw/core/profile.h"
#include "wlw/audio/audio.h"
#include "wlw/platform/bridge.h"
#include "wlw/platform/platform.h"
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

Application::Application(AppConfig config)
    : config_(std::move(config)),
      engine_(WLWEngine::Create()),
      window_(scene::Window::Create(config_.window_size, config_.title)),
      timestep_(config_.fixed_timestep, config_.max_frame_time) {
  LogThreadId();     // the main thread is T0 in log records
  profile::Start();  // names this thread "Game" in the profiler; log records go to its timeline
  // Log filters from the environment (desktop): WLW_LOG="Render=Verbose,Audio=Warning".
  if (const char* filter = std::getenv("WLW_LOG"); filter != nullptr && !SetLogFilter(filter)) {
    WLW_LOG(LogApp, Warning, "WLW_LOG=\"%s\": some parts aren't category=level", filter);
  }
  time_.fixed_step_ = config_.fixed_timestep;
  if (const std::string folder = platform::GetSaveDirectory(config_.title); !folder.empty()) {
    quality_.SetSaveFile(folder + "/quality.txt");
    cvar_file_ = folder + "/cvars.cfg";
#ifndef NDEBUG
    // Debug builds keep a log file (the run before as log.txt.prev); release builds don't write one.
    StartLogFile(folder + "/log.txt");
#endif
  }
  quit_command_ = std::make_unique<ConsoleCommand>("quit", "Quits the game", [this](const ConsoleArgs&) { Quit(); });
#if WLW_DEBUG_UI
  AddSystem<debug::EnginePanel>();  // the engine's own debug windows, first of the debug panels
  AddSystem<debug::CVarPanel>();
#endif
}

Application::~Application() {
  for (const auto& [cvar, id] : cvar_callbacks_) cvar->RemoveOnChange(id);
  profile::Stop();
}

int Application::Run() {
  ApplyStartupCVars();
  bool started = false;
  {
    WLW_PROFILE_SCOPE("Engine start");
    started = engine_->Start(window_);
  }
  if (!started) {
    WLW_LOG(LogApp, Error, "Engine failed to start");
    return 1;
  }
  audio_ = std::make_unique<audio::Audio>();
  // What else is playing on the device (Android: AudioWatcher.kt). A call, an alarm or the
  // assistant interrupts us: silent until it ends, and the game pauses like when it goes to the
  // background (the player has stopped playing). The player's own music: ours steps aside.
  platform::SetBridgeHandler("audio", [this](const std::string& event, const std::string&) {
    WLW_LOG(LogApp, Info, "Audio: %s", event.c_str());
    if (event == "interrupted") {
      if (!audio_->Interrupted()) screens_.AppPaused();
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
  ui_ = std::make_unique<ui::UIContext>(window_->GetScreen().GetCanvas());
#if WLW_DEBUG_UI
  debug_ui_ = std::make_unique<debug::DebugUI>(*window_);  // needs the GL context Start() made
#endif
  {
    WLW_PROFILE_SCOPE("Systems start");
    systems_.StartPending();  // the systems added before Run, in order
  }
  // The scenes they built: compile the shader variants they need now, not during the first frames.
  engine_->GetRenderingDriver()->WarmUp(*window_);
  // The game has started: ReadOnly cvars keep their values from now on. A quality lock from a config
  // file or the command line applies now that OnStart configured the levels.
  CVarBase::SetStarted(true);
  if (quality_.Configured() && cv_quality_lock.Get() >= 0) quality_.Lock(cv_quality_lock.Get());

  // steady_clock: monotonic (never jumps when the system clock changes), available on every platform.
  using Clock = std::chrono::steady_clock;
  Clock::time_point previous = Clock::now();
  const Clock::time_point start = previous;

  while (!quit_requested_ && !window_->ShouldClose()) {
    if (systems_.Started() < systems_.Size()) {
      WLW_PROFILE_SCOPE("Systems start");
      systems_.StartPending();  // systems added during the last frame start now
    }

    // 1. Lifecycle. Paused before resumed: coming back from the background, both can arrive in
    // one frame (the loop doesn't run while the app is away), and the game must end up paused
    // but audible.
    if (window_->ConsumePaused()) {
      WLW_PROFILE_SCOPE("App paused");
      audio_->Suspend();  // silent in the background
      ForEachSystem(systems_, [](System& system) { system.OnAppPaused(); });
      screens_.AppPaused();  // e.g. the game screen opens its pause panel
    }
    if (window_->ConsumeResumed()) {
      WLW_PROFILE_SCOPE("App resumed");
      // Back from the background (mobile): don't count the time we were away as one huge frame.
      previous = Clock::now();
      timestep_.Reset();
      quality_.Settle();  // the first frames back are slow for reasons that pass
      audio_->Resume();
      ForEachSystem(systems_, [](System& system) { system.OnAppResumed(); });
    }

    // 2. Time.
    const Clock::time_point now = Clock::now();
    const double frame_seconds = std::chrono::duration<double>(now - previous).count();
    previous = now;
    time_.since_start_ = std::chrono::duration<double>(now - start).count();
    // Clamp like the stepper does, so a breakpoint doesn't hand OnUpdate a 30-second dt.
    time_.real_delta_ = static_cast<float>(std::min(frame_seconds, static_cast<double>(config_.max_frame_time)));
    // Game time: real time through the time scale (hit-stop, slow motion). Once per frame.
    time_.delta_ = time_.scale_.Apply(time_.real_delta_);
    const int steps = timestep_.Advance(time_.delta_);
    time_.alpha_ = timestep_.Alpha();

#if WLW_DEBUG_UI
    {
      // First, so the debug UI can claim the pointer before the game reads input this frame.
      WLW_PROFILE_SCOPE("Debug UI begin");
      debug_ui_->BeginFrame(time_.real_delta_);
    }
#endif

    // 3. Back: the top screen first (close a panel...), then the systems; with nothing left to
    // close, leave like any Android app does (desktop: Escape quits).
    const input::Input& input = window_->GetInput();
    if (input.WasKeyPressed(input::Key::Back) || input.WasKeyPressed(input::Key::Escape)) {
      if (!screens_.Back() && !systems_.Back() && !platform::MoveToBackground()) Quit();
    }

    // 4. Results from platform code (an ad closed, a cloud save arrived...) reach the game here,
    // on its own thread, before this frame's updates.
    {
      WLW_PROFILE_SCOPE("Bridge messages");
      platform::DispatchBridgeMessages();
    }

    // 5-7. Updates and rendering: the systems, then the screens.
    const float step = timestep_.Step();
    for (int i = 0; i < steps; ++i) {
      WLW_PROFILE_SCOPE("FixedUpdate");
      ForEachSystem(systems_, [step](System& system) { system.OnFixedUpdate(step); });
      {
        WLW_PROFILE_SCOPE("Screens");
        screens_.FixedUpdate(step);
      }
    }
    {
      WLW_PROFILE_SCOPE("Update");
      const float dt = time_.delta_;
      ForEachSystem(systems_, [dt](System& system) { system.OnUpdate(dt); });
      {
        WLW_PROFILE_SCOPE("Screens");
        screens_.Update(dt);
      }
    }
    {
      WLW_PROFILE_SCOPE("Render");
      const float alpha = time_.alpha_;
      ForEachSystem(systems_, [alpha](System& system) { system.OnRender(alpha); });
      {
        WLW_PROFILE_SCOPE("Screens");
        screens_.Render(alpha);
      }
    }

    // 8. UI.
    BuildUI();

    // 9. Debug panels.
#if WLW_DEBUG_UI
    {
      WLW_PROFILE_SCOPE("DebugUI");
      if (debug_ui_->IsOpen()) {
        ForEachSystem(systems_, [](System& system) { system.OnDebugUI(); });
        screens_.DebugUI();
      }
      debug_ui_->EndFrame();  // drawn on top of the scene by Iterate()
    }
#endif

    // 10. Draw + present every window, then poll input; screen changes requested this frame
    // happen now, between frames.
    engine_->Iterate();
    WLW_PROFILE_FRAME();  // presented: one frame on the profiler's timeline
    {
      WLW_PROFILE_SCOPE("Screen changes");
      screens_.EndFrame(time_.real_delta_);
    }
    UpdateStats(frame_seconds);
    {
      WLW_PROFILE_SCOPE("Quality");
      UpdateQuality(frame_seconds);
    }
    if (ConsumeSavedCVarsChanged()) {
      WLW_PROFILE_SCOPE("Save cvars");
      SaveCVars();  // rare: a setting the player changed
    }
    PlotFrame(frame_seconds);
  }

  platform::SetBridgeHandler("audio", nullptr);
  WLW_PROFILE_SCOPE("Shutdown");
  screens_.Clear();  // OnExit for every screen while the systems are still fully alive
  ForEachSystem(systems_, [](System& system) { system.OnShutdown(); });
  return 0;
}

void Application::BuildUI() {
  WLW_PROFILE_SCOPE("RenderUI");
  const core::Vector2 framebuffer = window_->GetSize();
  const glm::vec2 reference{config_.ui_reference_size.x, config_.ui_reference_size.y};
  // Offscreen viewports' canvas layers: emptied and sized to their viewport (same units as the
  // UI's when proportioned like the screen); the game draws into them during OnRenderUI.
  for (const auto& [_, viewport] : window_->GetViewports()) {
    const glm::ivec2 pixels = viewport->PixelSize({framebuffer.x, framebuffer.y});
    viewport->BeginCanvases(glm::vec2(pixels), reference);
  }

  // The screen's layers; its default one is the game UI's canvas.
  window_->GetScreen().BeginCanvases({framebuffer.x, framebuffer.y}, reference);
  ui::Canvas& canvas = ui_->canvas;
  ui_->NewFrame(time_.RealDelta());
  ui_->safe_area = ui::SafeAreaFor(canvas, platform::GetSafeAreaInsets());

  // The pointer for UI widgets, in Canvas units: pointer units -> framebuffer pixels -> units.
  // Uses the normal (blockable) queries, so the debug UI still takes precedence.
  const input::Input& input = window_->GetInput();
  const core::Vector2 surface = input.SurfaceSize();
  ui::PointerState pointer;
  if (surface.x > 0.0f && surface.y > 0.0f) {
    const core::Vector2 p = input.PointerPosition();
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
    screens_.RenderUI(ui);
  }
  if (const float fade = screens_.FadeAmount(); fade > 0.0f) {  // screen transitions
    canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, {0.0f, 0.0f, 0.0f, fade});
  }
}

void Application::UpdateQuality(double frame_seconds) {
  if (!quality_.Configured()) return;
  if (!quality_timing_) {  // it decides from the GPU's time where the GPU can measure it
    quality_timing_ = true;
    if (window_->GpuTimingSupported()) cv_gpu_timing.Set(true);
  }
  const float gpu_ms = window_->GpuTiming() ? window_->GpuFrameMs() : -1.0f;
  quality_.Update(static_cast<float>(frame_seconds), gpu_ms, static_cast<float>(frame_seconds * 1000.0));
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
  cvar_callbacks_.emplace_back(&cv_log_filter, cv_log_filter.OnChange([] {
    SetLogLevel(LogLevel::Info);  // a new filter replaces the old one
    if (!SetLogFilter(cv_log_filter.Get())) WLW_LOG(LogApp, Warning, "log.filter: some parts aren't category=level");
  }));
  WatchCVar(cv_gpu_timing, [this] { window_->SetGpuTiming(cv_gpu_timing.Get()); });
  WatchCVar(cv_invalidate_framebuffers,
            [this] { window_->SetInvalidateFramebuffers(cv_invalidate_framebuffers.Get()); });
  cvar_callbacks_.emplace_back(&cv_quality_lock, cv_quality_lock.OnChange([this] {
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
  cvar_callbacks_.emplace_back(&cvar, cvar.OnChange(std::move(apply)));
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
    const scene::RenderStats& stats = window_->GetScreen().GetRenderStats();
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

  if (config_.show_stats_in_title) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), " | %.0f fps | %.1f ms", time_.fps_, time_.frame_ms_);
    window_->SetTitle(config_.title + buffer);
  }
}

}  // namespace wlw::app
