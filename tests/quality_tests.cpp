// Tests for quality levels (app/quality.h): stepping down when frames don't fit, up when there's
// room (slowly, longer after a level failed), settling after a change, the frame-time-only mode
// (no GPU timing: down only), locking, and the level kept between launches.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/app/quality.h"

TEST_SUITE_BEGIN("quality");

using namespace wlw;
using app::Quality;
using app::QualitySettings;

namespace {

constexpr float kFrame = 1.0f / 60.0f;

// `seconds` of frames with these readings (gpu_ms < 0: not measured). Returns the level changes.
int Run(Quality& quality, float seconds, float gpu_ms, float frame_ms = 16.7f) {
  int changes = 0;
  for (float t = 0.0f; t < seconds; t += kFrame) changes += quality.Update(kFrame, gpu_ms, frame_ms) ? 1 : 0;
  return changes;
}

}  // namespace

TEST_CASE("starts highest and settles") {
  Quality quality;
  CHECK((!quality.Configured() && !quality.Update(kFrame, 50.0f, 50.0f)));  // nothing until configured
  std::vector<int> seen;
  quality.OnChange([&seen](int level) { seen.push_back(level); });
  quality.Configure({.levels = 3});
  CHECK((quality.Level() == 2 && seen == std::vector<int>{2}));  // told at once
  // The first seconds (warm-up) are ignored, however slow.
  CHECK((Run(quality, 2.9f, 30.0f) == 0 && quality.Level() == 2));
}

TEST_CASE("steps down when the gpu is busy") {
  Quality quality;
  int last = -1;
  quality.OnChange([&last](int level) { last = level; });
  quality.Configure({.levels = 3, .settle_seconds = 0.0f});
  // 15 ms of GPU work in a 16.6 ms frame: over 85%, too close.
  CHECK(Run(quality, 1.5f, 15.0f) == 0);  // not yet: needs 2 seconds of it
  CHECK((Run(quality, 1.0f, 15.0f) == 1 && quality.Level() == 1 && last == 1));
  // A short spike doesn't count: it has to last.
  Run(quality, 1.0f, 8.0f);
  Run(quality, 0.5f, 30.0f);
  CHECK((Run(quality, 1.0f, 8.0f) == 0 && quality.Level() == 1));
  // In between (not busy, not much room): stays.
  CHECK((Run(quality, 30.0f, 12.0f) == 0 && quality.Level() == 1));
  // Can't go below 0.
  Run(quality, 10.0f, 30.0f);
  CHECK(quality.Level() == 0);
}

TEST_CASE("steps down when frames are late") {
  // The GPU may be fine and the frame still late (CPU work: the particles' update): frames over
  // budget x 1.25 step down too.
  Quality quality;
  quality.Configure({.levels = 3, .settle_seconds = 0.0f});
  CHECK((Run(quality, 2.5f, 5.0f, 33.0f) == 1 && quality.Level() == 1));
  // A one-frame hitch (a level loading) counts as at most two budgets and fades: no step.
  Quality hitch;
  hitch.Configure({.levels = 3, .settle_seconds = 0.0f});
  hitch.Update(kFrame, 5.0f, 2000.0f);
  CHECK((Run(hitch, 5.0f, 5.0f) == 0 && hitch.Level() == 2));
}

TEST_CASE("steps up slowly and backs off") {
  Quality quality;
  quality.Configure({.levels = 3, .start = 0, .settle_seconds = 1.0f});
  CHECK(quality.Level() == 0);
  // Plenty of room (under 55%): up after 10 s, plus the settling.
  CHECK(Run(quality, 10.5f, 4.0f) == 0);
  CHECK((Run(quality, 1.0f, 4.0f) == 1 && quality.Level() == 1));
  // Level 2 turns out too slow: back down, and level 2 now waits twice as long to be tried.
  Run(quality, 11.5f, 4.0f);
  CHECK(quality.Level() == 2);
  Run(quality, 3.5f, 15.0f);
  CHECK((quality.Level() == 1 && Near(quality.UpWait(2), 20.0f)));
  CHECK((Run(quality, 11.5f, 4.0f) == 0 && quality.Level() == 1));  // 10 s isn't enough any more
  CHECK((Run(quality, 10.0f, 4.0f) == 1 && quality.Level() == 2));
  // The wait doubles up to 8 x, no more.
  Quality many;
  many.Configure({.levels = 2, .settle_seconds = 0.0f});
  for (int i = 0; i < 6; ++i) {
    Run(many, 4.0f, 15.0f);   // down (the smoothed reading takes ~1.3 s to climb from 4 ms, then 2 s)
    Run(many, 100.0f, 4.0f);  // up again
  }
  CHECK(Near(many.UpWait(1), 80.0f));
}

TEST_CASE("without gpu timing only steps down") {
  // No timer queries: vsync makes a fast frame look like a slow one, so there's no "room" to see.
  Quality quality;
  quality.Configure({.levels = 3, .start = 1, .settle_seconds = 0.0f});
  CHECK((Run(quality, 60.0f, -1.0f, 16.7f) == 0 && quality.Level() == 1));
  CHECK((Run(quality, 2.5f, -1.0f, 33.0f) == 1 && quality.Level() == 0));
}

TEST_CASE("lock") {
  Quality quality;
  std::vector<int> seen;
  quality.OnChange([&seen](int level) { seen.push_back(level); });
  quality.Configure({.levels = 3, .settle_seconds = 1.0f});
  quality.Lock(0);
  CHECK((quality.Locked() && quality.Level() == 0 && seen.back() == 0));
  CHECK((Run(quality, 30.0f, 2.0f) == 0 && quality.Level() == 0));  // no adapting while locked
  quality.Lock(9);
  CHECK(quality.Level() == 2);  // clamped
  quality.Unlock();
  CHECK((!quality.Locked() && Run(quality, 2.9f, 30.0f) == 0));  // settles first (1 s), then 2 s of it
  CHECK((Run(quality, 2.5f, 30.0f) == 1 && quality.Level() == 1));
}

TEST_CASE("saved between launches") {
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "wlw_quality_test.txt";
  std::filesystem::remove(file);
  {
    Quality quality;
    quality.SetSaveFile(file.string());
    quality.Configure({.levels = 3, .settle_seconds = 0.0f});
    CHECK(quality.Level() == 2);  // nothing saved: the highest
    Run(quality, 2.5f, 15.0f);
    CHECK(quality.Level() == 1);
    quality.Lock(0);  // a lock isn't saved
  }
  {
    Quality next_launch;
    next_launch.SetSaveFile(file.string());
    next_launch.Configure({.levels = 3});
    CHECK(next_launch.Level() == 1);
  }
  {
    Quality fewer_levels;  // the game changed its levels since: clamped
    fewer_levels.SetSaveFile(file.string());
    fewer_levels.Configure({.levels = 1});
    CHECK(fewer_levels.Level() == 0);
  }
  {
    Quality forced;  // the game's start wins over the saved one
    forced.SetSaveFile(file.string());
    forced.Configure({.levels = 3, .start = 2});
    CHECK(forced.Level() == 2);
  }
  std::filesystem::remove(file);
}

TEST_SUITE_END();
