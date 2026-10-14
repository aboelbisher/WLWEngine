// Tests for wlw::kit: save files (backup, corruption, migrations), dates and streaks, merging
// progress from two devices, the desktop fake ads and the interstitial pacing rules.
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/kit/ad_pacing.h"
#include "wlw/kit/ads.h"
#include "wlw/kit/daily.h"
#include "wlw/kit/progress.h"
#include "wlw/kit/save_store.h"
#include "wlw/platform/platform.h"

TEST_SUITE_BEGIN("kit");

using namespace wlw::kit;
namespace fs = std::filesystem;

// One folder per case: ctest runs the cases in parallel processes.
static std::string TestFolder(const std::string& name) {
  const fs::path folder = fs::temp_directory_path() / "wlw_kit_tests" / name;
  fs::remove_all(folder);
  fs::create_directories(folder);
  return folder.string();
}

static void WriteText(const std::string& path, const std::string& text) {
  wlw::platform::WriteFileAtomic(path, std::vector<uint8_t>(text.begin(), text.end()));
}

TEST_CASE("save and load") {
  const std::string folder = TestFolder("save_and_load");
  {
    SaveStore store(folder, "progress", 1);
    CHECK(store.Load() == SaveStore::LoadResult::Fresh);
    CHECK((store.Data().is_object() && store.Data().empty()));
    CHECK(store.SaveIfDirty());  // nothing to do
    CHECK(!fs::exists(store.Path()));
    store.Data()["best"] = 1200;
    store.MarkDirty();
    CHECK(store.SaveIfDirty());
    CHECK(!store.Dirty());
  }
  SaveStore again(folder, "progress", 1);
  CHECK(again.Load() == SaveStore::LoadResult::Loaded);
  CHECK(again.Data().value("best", 0) == 1200);
  CHECK(!again.Dirty());
  fs::remove_all(folder);
}

TEST_CASE("backup survives a corrupt file") {
  const std::string folder = TestFolder("backup_survives_a_corrupt_file");
  SaveStore store(folder, "progress", 1);
  store.Load();
  store.Data()["best"] = 100;
  store.Save();
  store.Data()["best"] = 200;
  store.Save();                                                        // the 100 version is now the backup
  WriteText(store.Path(), "{\"version\": 1, \"data\": {\"best\": 2");  // cut off mid-write

  SaveStore reloaded(folder, "progress", 1);
  CHECK(reloaded.Load() == SaveStore::LoadResult::FromBackup);
  CHECK(reloaded.Data().value("best", 0) == 100);
  CHECK(reloaded.Dirty());  // the broken main file gets repaired at the next save

  WriteText(store.Path(), "not json at all");
  WriteText(store.Path() + ".bak", "[1, 2, 3]");  // valid JSON, wrong shape
  SaveStore fresh(folder, "progress", 1);
  CHECK(fresh.Load() == SaveStore::LoadResult::Fresh);
  CHECK(fresh.Data().empty());

  fresh.Wipe();
  CHECK((!fs::exists(store.Path()) && !fs::exists(store.Path() + ".bak")));
  fs::remove_all(folder);
}

TEST_CASE("migrations") {
  const std::string folder = TestFolder("migrations");
  // Version 1 stored "highscore"; version 2 renamed it to "best"; version 3 added "games".
  WriteText(folder + "/progress.json", "{\"version\": 1, \"data\": {\"highscore\": 77}}");
  SaveStore store(folder, "progress", 3);
  store.AddMigration(1, [](Json& data) {
    data["best"] = data.value("highscore", 0);
    data.erase("highscore");
  });
  store.AddMigration(2, [](Json& data) { data["games"] = 0; });
  CHECK(store.Load() == SaveStore::LoadResult::Loaded);
  CHECK(store.Data().value("best", 0) == 77);
  CHECK(!store.Data().contains("highscore"));
  CHECK(store.Data().contains("games"));
  CHECK(store.Dirty());  // saves the upgraded format
  store.Save();

  SaveStore reread(folder, "progress", 3);
  reread.Load();
  CHECK(!reread.Dirty());
  CHECK(reread.Data().value("best", 0) == 77);
  fs::remove_all(folder);
}

TEST_CASE("device id") {
  const std::string folder = TestFolder("device_id");
  SaveStore device(folder, "device", 1);
  device.Load();
  const std::string id = DeviceId(device);
  CHECK(id.size() == 16);
  CHECK(device.Dirty());
  CHECK(DeviceId(device) == id);  // created once
  device.Save();
  SaveStore reloaded(folder, "device", 1);
  reloaded.Load();
  CHECK(DeviceId(reloaded) == id);
  fs::remove_all(folder);
}

static Date D(int year, int month, int day) { return {year, month, day}; }

TEST_CASE("dates") {
  CHECK(D(1970, 1, 1).DayNumber() == 0);
  CHECK(D(2000, 3, 1).DayNumber() - D(2000, 2, 28).DayNumber() == 2);  // 2000 is a leap year
  CHECK(D(1900, 3, 1).DayNumber() - D(1900, 2, 28).DayNumber() == 1);  // 1900 isn't
  CHECK(D(2026, 12, 31).AddDays(1) == D(2027, 1, 1));
  CHECK(D(2028, 2, 28).AddDays(1) == D(2028, 2, 29));
  CHECK(D(2026, 3, 1).AddDays(-1) == D(2026, 2, 28));
  CHECK(Date::FromNumber(20261009) == D(2026, 10, 9));
  CHECK(D(2026, 10, 9).ToNumber() == 20261009);
  // Round trip over a long range, including before 1970.
  bool round_trip = true;
  for (int64_t day = -800000; day <= 800000; day += 37) round_trip &= Date::FromDayNumber(day).DayNumber() == day;
  CHECK(round_trip);
  const Date today = Today();
  CHECK((today.year >= 2024 && today.month >= 1 && today.month <= 12 && today.day >= 1 && today.day <= 31));
  CHECK(NowSeconds() > 1700000000);
}

TEST_CASE("streaks") {
  DailyLog log;
  CHECK(log.CurrentStreak(20270102) == 0);
  CHECK(log.Mark(20261230));
  CHECK(log.Mark(20261231));
  CHECK(log.Mark(20270101));  // across the new year
  CHECK(!log.Mark(20270101));
  CHECK(log.CurrentStreak(20270101) == 3);
  CHECK(log.CurrentStreak(20270102) == 3);  // today not played yet: the streak is still alive
  CHECK(log.CurrentStreak(20270103) == 0);  // missed a day
  log.Mark(20270103);
  CHECK(log.CurrentStreak(20270103) == 1);
  log.Mark(20260227);
  log.Mark(20260228);
  log.Mark(20260301);  // across a month end
  log.Mark(20260302);
  CHECK(log.BestStreak() == 4);
  CHECK(DailyLog::FromJson(log.ToJson()) == log);
}

TEST_CASE("counters") {
  Counters phone, tablet;
  phone.Add("phone", "games", 5);
  tablet.Add("tablet", "games", 3);
  Counters merged = phone;
  merged.Merge(tablet);
  CHECK(merged.Get("games") == 8);  // games on both devices count
  merged.Merge(tablet);             // merging the same copy again changes nothing
  merged.Merge(merged);
  CHECK(merged.Get("games") == 8);
  // The phone played 2 more, then synced again with the old merged copy: max per device, no double count.
  phone.Add("phone", "games", 2);
  phone.Merge(merged);
  CHECK(phone.Get("games") == 10);
  phone.Add("phone", "games", -4);  // ignored: counters only grow
  CHECK(phone.Get("games") == 10);
  CHECK(phone.Get("unknown") == 0);
  CHECK(Counters::FromJson(phone.ToJson()) == phone);
}

static int Submit(Scores& scores, int64_t score, int date, int64_t time, const char* device) {
  return scores.Submit({score, date, time, device});
}

TEST_CASE("scores") {
  Scores scores(3);
  CHECK(scores.Best() == 0);
  CHECK(Submit(scores, 100, 20261001, 1000, "a") == 0);
  CHECK(Submit(scores, 300, 20261002, 2000, "a") == 0);
  CHECK(Submit(scores, 200, 20261003, 3000, "a") == 1);
  CHECK(Submit(scores, 50, 20261004, 4000, "a") == -1);  // the table is full
  CHECK(Submit(scores, 0, 20261004, 5000, "a") == -1);
  CHECK(scores.Best() == 300);
  CHECK(Submit(scores, 200, 20261005, 6000, "a") == 2);  // a tie goes below the older score
  CHECK(scores.Entries().size() == 3);
  CHECK(scores.Entries()[2].time == 6000);

  // Two devices merge into the best 3 of both; identical entries (the same game) appear once.
  Scores other(3);
  other.Submit({250, 20261006, 7000, "b"});
  other.Submit({300, 20261002, 2000, "a"});  // a copy of device a's game
  Scores merged = scores;
  merged.Merge(other);
  CHECK(merged.Entries().size() == 3);
  CHECK((merged.Entries()[0].score == 300 && merged.Entries()[1].score == 250 && merged.Entries()[2].score == 200));
  Scores reverse = other;
  reverse.Merge(scores);
  CHECK(reverse == merged);  // order of merging doesn't matter
  CHECK(Scores::FromJson(merged.ToJson(), 3) == merged);

  // The same game submitted again (continued after game over): one entry, its higher score.
  Scores game(3);
  CHECK(Submit(game, 100, 20261007, 9000, "a") == 0);
  CHECK(Submit(game, 90, 20261007, 9000, "a") == -1);
  CHECK(Submit(game, 180, 20261007, 9000, "a") == 0);
  CHECK((game.Entries().size() == 1 && game.Best() == 180));
  CHECK(Submit(game, 180, 20261007, 9000, "b") == 1);  // another device: another game
}

TEST_CASE("levels") {
  LevelProgress a;
  CHECK(!a.Completed("1"));
  CHECK(a.Complete("1", 2));
  CHECK(!a.Complete("1", 1));  // worse: ignored
  CHECK(a.Complete("1", 3));
  CHECK(a.Complete("2", 0));
  CHECK((a.Completed("2") && a.Stars("2") == 0));
  LevelProgress b;
  b.Complete("2", 2);
  b.Complete("3", 1);
  a.Merge(b);
  CHECK((a.Stars("1") == 3 && a.Stars("2") == 2 && a.Stars("3") == 1));
  CHECK((a.TotalStars() == 6 && a.CompletedCount() == 3));
  CHECK(LevelProgress::FromJson(a.ToJson()) == a);
}

TEST_CASE("fake ads") {
  FakeAds ads(2.0f);
  CHECK((ads.RewardedReady() && ads.InterstitialReady() && !ads.Showing()));
  int rewards = 0, misses = 0;
  ads.ShowRewarded([&](bool rewarded) { rewarded ? ++rewards : ++misses; });
  CHECK((ads.Showing() && ads.ShowingRewarded()));
  ads.ShowRewarded([&](bool rewarded) { rewarded ? ++rewards : ++misses; });  // one at a time
  CHECK(misses == 1);
  ads.Update(1.0f);
  CHECK((ads.Showing() && Near(ads.Progress(), 0.5f) && rewards == 0));
  ads.Update(1.0f);
  CHECK((!ads.Showing() && rewards == 1));

  ads.ShowRewarded([&](bool rewarded) { rewarded ? ++rewards : ++misses; });
  ads.Skip();  // closed early: no reward
  ads.Update(0.1f);
  CHECK((rewards == 1 && misses == 2));

  bool closed = false;
  ads.ShowInterstitial([&] { closed = true; });
  CHECK((ads.Showing() && !ads.ShowingRewarded()));
  ads.Update(2.5f);
  CHECK(closed);

  ads.available = false;  // no fill: the callback answers at once
  CHECK(!ads.RewardedReady());
  ads.ShowRewarded([&](bool rewarded) { rewarded ? ++rewards : ++misses; });
  CHECK((misses == 3 && !ads.Showing()));
  closed = false;
  ads.ShowInterstitial([&] { closed = true; });
  CHECK(closed);
}

TEST_CASE("ad pacing") {
  AdPacing pacing({/*first_after_games=*/3, /*games_between=*/3, /*min_seconds_between=*/120.0});
  pacing.GameFinished();
  pacing.GameFinished();
  CHECK(!pacing.InterstitialDue(1000.0));  // the first games are ad-free
  pacing.GameFinished();
  CHECK(pacing.InterstitialDue(1000.0));
  pacing.InterstitialShown(1000.0);
  for (int i = 0; i < 3; ++i) pacing.GameFinished();
  CHECK(!pacing.InterstitialDue(1100.0));  // 3 games, but only 100 s later
  CHECK(pacing.InterstitialDue(1120.0));
  pacing.RewardedWatched(1130.0);  // the player chose to watch one: that counts
  CHECK(!pacing.InterstitialDue(1130.0));
  CHECK((pacing.GamesSinceAd() == 0 && pacing.GamesFinished() == 6));
}

TEST_SUITE_END();
