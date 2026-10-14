#include "wlw/app/quality.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace wlw::app {

namespace {

// The saved level: the file holds just the number.
int LoadLevel(const std::string& file) {
  if (file.empty()) return -1;
  const auto data = platform::ReadLocalFile(file);
  if (!data || data->empty()) return -1;
  const std::string text(data->begin(), data->end());
  char* end = nullptr;
  const long level = std::strtol(text.c_str(), &end, 10);
  return end != text.c_str() && level >= 0 ? static_cast<int>(level) : -1;
}

}  // namespace

void Quality::Configure(const QualitySettings& settings) {
  settings_ = settings;
  settings_.levels = std::clamp(settings_.levels, 1, kMaxLevels);
  failures_ = {};
  locked_ = false;
  configured_ = true;
  const int saved = settings_.start >= 0 ? -1 : LoadLevel(save_file_);
  int level = settings_.start >= 0 ? settings_.start : saved;
  if (level < 0) level = settings_.levels - 1;
  level_ = std::min(level, settings_.levels - 1);
  WLW_LOG(LogApp, Info, "Quality: level %d of 0..%d%s", level_, settings_.levels - 1, saved >= 0 ? " (saved)" : "");
  Settle();
  if (on_change_) on_change_(level_);
}

void Quality::OnChange(std::function<void(int)> callback) {
  on_change_ = std::move(callback);
  if (on_change_ && configured_) on_change_(level_);
}

void Quality::Lock(int level) {
  locked_ = true;
  level = std::clamp(level, 0, settings_.levels - 1);
  if (level == level_) return;
  level_ = level;  // not saved: the game keeps a player's choice itself
  if (on_change_) on_change_(level_);
}

void Quality::Unlock() {
  locked_ = false;
  Settle();
}

void Quality::Settle() {
  settle_ = settings_.settle_seconds;
  gpu_ms_ = -1.0f;
  frame_ms_ = -1.0f;
  over_ = 0.0f;
  under_ = 0.0f;
}

float Quality::UpWait(int level) const {
  if (level < 0 || level >= kMaxLevels) return settings_.up_seconds;
  int backoff = 1;
  for (int i = 0; i < failures_[static_cast<size_t>(level)] && backoff < kMaxBackoff; ++i) backoff *= 2;
  return settings_.up_seconds * static_cast<float>(backoff);
}

void Quality::SetTargetFrameMs(float ms) {
  if (ms <= 0.0f || std::abs(ms - target_frame_ms_) < 0.01f) return;  // the same rate (float noise)
  target_frame_ms_ = ms;
  if (configured_ && settings_.budget_ms <= 0.0f) Settle();  // readings against the old budget mean nothing now
}

bool Quality::Update(float dt, float gpu_ms, float frame_ms) {
  if (!configured_ || locked_ || dt <= 0.0f) return false;
  if (settle_ > 0.0f) {
    settle_ -= dt;
    return false;
  }

  // Smoothed over about half a second; a hitch (a level loading) counts as at most two budgets.
  const float budget = BudgetMs();
  const float blend = 1.0f - std::exp(-dt / 0.5f);
  auto smooth = [budget, blend](float& average, float sample) {
    sample = std::min(sample, 2.0f * budget);
    average = average < 0.0f ? sample : average + (sample - average) * blend;
  };
  const bool timed = gpu_ms >= 0.0f;
  if (timed) smooth(gpu_ms_, gpu_ms);
  if (frame_ms > 0.0f) smooth(frame_ms_, frame_ms);

  const bool late = frame_ms_ > budget * settings_.slow_frame_at;
  const bool too_slow = late || (timed && gpu_ms_ > budget * settings_.down_at);
  const bool room = timed && !late && gpu_ms_ < budget * settings_.up_at;
  if (too_slow) {
    over_ += dt;
    under_ = 0.0f;
  } else if (room) {
    under_ += dt;
    over_ = 0.0f;
  } else {
    over_ = 0.0f;
    under_ = 0.0f;
  }

  if (over_ >= settings_.down_seconds && level_ > 0) {
    int& failures = failures_[static_cast<size_t>(level_)];
    failures = std::min(failures + 1, 30);
    WLW_LOG(LogApp, Info, "Quality: %d -> %d (GPU %.1f ms, frame %.1f ms)", level_, level_ - 1, gpu_ms_, frame_ms_);
    SetLevel(level_ - 1);
    return true;
  }
  if (level_ + 1 < settings_.levels && under_ >= UpWait(level_ + 1)) {
    WLW_LOG(LogApp, Info, "Quality: %d -> %d (GPU %.1f ms, frame %.1f ms)", level_, level_ + 1, gpu_ms_, frame_ms_);
    SetLevel(level_ + 1);
    return true;
  }
  return false;
}

void Quality::SetLevel(int level) {
  level_ = level;
  Settle();
  Save();
  if (on_change_) on_change_(level_);
}

void Quality::Save() const {
  if (save_file_.empty()) return;
  const std::string text = std::to_string(level_);
  if (!platform::WriteFileAtomic(save_file_, std::vector<uint8_t>(text.begin(), text.end()))) {
    WLW_LOG(LogApp, Warning, "Quality: couldn't save the level to %s", save_file_.c_str());
  }
}

}  // namespace wlw::app
