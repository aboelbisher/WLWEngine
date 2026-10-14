#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>

// Quality levels picked from what the device can do, like Unity's QualitySettings + Adaptive
// Performance: the GAME says what each level means (how many particles, how big the buffers),
// this decides which one runs. It watches the GPU's time per frame (Window::GpuFrameMs, the timer
// queries) and the frame time, steps down quickly when the frame doesn't fit, back up slowly when
// there is plenty of room, and saves the level that worked so the next launch starts there.
//
//   App().Quality().Configure({.levels = 3});
//   App().Quality().OnChange([this](int level) { ApplyTier(kTiers[level]); });  // also called now
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
  float budget_ms = 16.6f;      // the frame time to fit in (16.6 = 60 fps)
  float down_at = 0.85f;        // GPU time above budget x this ...
  float slow_frame_at = 1.25f;  // ... or frames longer than budget x this (missing vsync) ...
  float down_seconds = 2.0f;    // ... for this long: one level down
  float up_at = 0.55f;          // GPU time below budget x this (and frames on time) ...
  float up_seconds = 10.0f;     // ... for this long: one level up; doubled each time that level failed
  float settle_seconds = 3.0f;  // readings ignored after a change or a resume (new buffers, warm-up)
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
  // Called with the level when it changes, and once at once (with the current one) if configured.
  void OnChange(std::function<void(int)> callback);

  // A fixed level, no adapting (a settings menu, tests, screenshots); Unlock resumes from it.
  void Lock(int level);
  void Unlock();
  bool Locked() const { return locked_; }

  // ---- For the Application (and tests) ----
  // Where the level is kept between launches (empty: nowhere). Set before Configure.
  void SetSaveFile(std::string path) { save_file_ = std::move(path); }
  // Fed once per frame.
  // `dt` seconds of real time; `gpu_ms` the GPU's time for the frame (< 0: not measured);
  // `frame_ms` the time between frames. Returns true when the level changed (the callback has run).
  bool Update(float dt, float gpu_ms, float frame_ms);
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

  QualitySettings settings_;
  std::string save_file_;
  std::function<void(int)> on_change_;
  bool configured_ = false;
  bool locked_ = false;
  int level_ = 0;
  float settle_ = 0.0f;
  float gpu_ms_ = -1.0f;
  float frame_ms_ = -1.0f;
  float over_ = 0.0f;
  float under_ = 0.0f;
  std::array<int, kMaxLevels> failures_{};  // times each level was stepped down from
};

}  // namespace wlw::app
