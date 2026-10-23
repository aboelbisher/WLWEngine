#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Quality levels picked from what the device can do, like Unity's QualitySettings + Adaptive
// Performance: the GAME says what each level means (how many particles, how big the buffers),
// this decides which one runs. It watches the GPU's time per frame (Window::GpuFrameMs, the timer
// queries) and the frame time, steps down quickly when the frame doesn't fit, back up slowly when
// there is plenty of room, and saves the level that worked so the next launch starts there.
//
//   App().Quality().Configure({.levels = 3});
//   App().Quality().OnChange([this](int level) { ApplyTier(kTiers[level]); });  // also called now
//
// The engine can apply one thing itself: the screen's render scale per level
// (Configure({.levels = 3, .render_scales = {0.7f, 0.85f, 1.0f}})).
//
// Without GPU timing (some drivers, the emulator) the frame time is all there is, and vsync hides
// any room left (a fast frame still takes 16.7 ms): it then only steps down.
//
// The logic is plain C++ fed readings (Update), so it is tested without a GPU; the Application
// feeds it once per frame and runs the callback between frames.
namespace wlw::app {

struct QualitySettings {
  int levels = 3;               // 0 = the lowest
  int start = -1;               // the level before anything is measured: -1 = the saved one, else the highest
  float budget_ms = 0.0f;       // the frame time to fit in; 0 = the frame-rate target's (16.7 at 60 fps)
  float down_at = 0.85f;        // GPU time above budget x this ...
  float slow_frame_at = 1.25f;  // ... or frames longer than budget x this (missing vsync) ...
  float down_seconds = 2.0f;    // ... for this long: one level down
  float up_at = 0.55f;          // GPU time below budget x this (and frames on time) ...
  float up_seconds = 10.0f;     // ... for this long: one level up; doubled each time that level failed
  float settle_seconds = 3.0f;  // readings ignored after a change or a resume (new buffers, warm-up)
  // The screen's render scale per level, lowest level first (Viewport::SetRenderScale: the 3D
  // drawn smaller and stretched, the UI sharp). The Application applies it whenever the level
  // changes; levels past the list's end use its last one. Empty: the engine leaves it to the game.
  std::vector<float> render_scales;
};

class Quality {
 public:
  static constexpr int kMaxLevels = 8;
  static constexpr int kMaxBackoff = 8;  // a failed level waits at most up_seconds x this to retry

  // Starts adapting with `settings` (and turns GPU timing on, where the GPU has it).
  void Configure(const QualitySettings& settings);
  bool Configured() const { return configured_; }
  const QualitySettings& Settings() const { return settings_; }

  int Level() const { return level_; }
  // The screen's render scale for the current level (QualitySettings::render_scales); 0 = none
  // given (or not configured).
  float RenderScale() const;
  // Called with the level when it changes, and once at once (with the current one) if configured.
  void OnChange(std::function<void(int)> callback);

  // A fixed level, no adapting (a settings menu, tests, screenshots); Unlock resumes from it.
  void Lock(int level);
  void Unlock();
  bool Locked() const { return locked_; }

  // The frame time it fits in: settings' budget_ms, else the frame-rate target's (SetTargetFrameMs).
  float BudgetMs() const { return settings_.budget_ms > 0.0f ? settings_.budget_ms : target_frame_ms_; }

  class InternalAccess;
  // Feeding it (the Application's, and tests'): see InternalAccess below.
  InternalAccess Internal();

  // Ignore readings for settle_seconds (after the app was in the background, a level loaded...).
  void Settle();

  // For the debug panel: the smoothed readings (< 0: none yet) and why it would move.
  float SmoothedGpuMs() const { return gpu_ms_; }
  float SmoothedFrameMs() const { return frame_ms_; }
  float SecondsOver() const { return over_; }
  float SecondsUnder() const { return under_; }
  float UpWait(int level) const;  // how long `level` must have room before it's tried again

 private:
  void SetLevel(int level);
  void Save() const;
  void SetTargetFrameMs(float ms);
  bool Update(float dt, float gpu_ms, float frame_ms);

  QualitySettings settings_;
  std::string save_file_;
  std::function<void(int)> on_change_;
  bool configured_ = false;
  bool locked_ = false;
  int level_ = 0;
  float target_frame_ms_ = 1000.0f / 60.0f;
  float settle_ = 0.0f;
  float gpu_ms_ = -1.0f;
  float frame_ms_ = -1.0f;
  float over_ = 0.0f;
  float under_ = 0.0f;
  std::array<int, kMaxLevels> failures_{};  // times each level was stepped down from
};

// For the Application (and tests), not games.
class Quality::InternalAccess {
 public:
  explicit InternalAccess(Quality& quality) : quality_(quality) {}

  // Where the level is kept between launches (empty: nowhere). Set before Configure.
  void SetSaveFile(std::string path) const { quality_.save_file_ = std::move(path); }
  // The frame time of the rate frames are paced to (the Application keeps it in step with the
  // window). A change while it is the budget restarts the readings (Settle).
  void SetTargetFrameMs(float ms) const { quality_.SetTargetFrameMs(ms); }
  // Fed once per frame: `dt` seconds of real time; `gpu_ms` the GPU's time for the frame (< 0: not
  // measured); `frame_ms` the time between frames. Returns true when the level changed (the callback has run).
  bool Update(float dt, float gpu_ms, float frame_ms) const { return quality_.Update(dt, gpu_ms, frame_ms); }

 private:
  Quality& quality_;
};

inline Quality::InternalAccess Quality::Internal() { return InternalAccess(*this); }

}  // namespace wlw::app
