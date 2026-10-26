// Tests for wlw::kit: save files (backup, corruption, migrations), dates and streaks, merging
// progress from two devices, the desktop fake ads, the interstitial pacing rules and leaderboards
// (the fake, and the bridge side fed with the Kotlin service's messages).
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/kit/ad_pacing.h"
#include "wlw/kit/ads.h"
#include "wlw/kit/daily.h"
#include "wlw/kit/leaderboards.h"
#include "wlw/kit/progress.h"
#include "wlw/kit/save_store.h"
#include "wlw/platform/bridge.h"
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

TEST_CASE("fake leaderboards") {
  FakeLeaderboards boards(0.5f);
  boards.AddRivals("classic", 40, 10000, 7);
  CHECK(boards.State() == Leaderboards::SignInState::SignedIn);

  // Answers come later, in Update - never inside Load.
  LeaderboardPage all, today;
  int answers = 0;
  boards.Load("classic", LeaderboardSpan::AllTime, 10, [&](const LeaderboardPage& page, bool ok) {
    CHECK(ok);
    all = page;
    ++answers;
  });
  boards.Load("classic", LeaderboardSpan::Today, 10, [&](const LeaderboardPage& page, bool ok) {
    CHECK(ok);
    today = page;
    ++answers;
  });
  CHECK(answers == 0);
  boards.Update(0.3f);
  CHECK(answers == 0);
  boards.Update(0.3f);
  REQUIRE(answers == 2);
  CHECK((all.players == 40 && all.top.size() == 10 && !all.player));
  CHECK((today.players > 0 && today.players < 40));  // fewer rivals played today
  for (size_t i = 0; i < all.top.size(); ++i) {
    CHECK(all.top[i].rank == static_cast<int64_t>(i) + 1);
    if (i > 0) CHECK(all.top[i - 1].score >= all.top[i].score);
  }

  // The player's best counts in every span; a lower score changes nothing.
  boards.Submit("classic", 9999999);
  boards.Submit("classic", 5);
  boards.Load("classic", LeaderboardSpan::Today, 3, [&](const LeaderboardPage& page, bool ok) {
    CHECK(ok);
    today = page;
  });
  boards.Update(1.0f);
  REQUIRE(today.player);
  CHECK((today.player->rank == 1 && today.player->score == 9999999 && today.player->is_player));
  CHECK((today.top.size() == 3 && today.top[0].is_player && !today.top[1].is_player));

  // A low score outside the top: the page still says where the player stands.
  FakeLeaderboards low(0.0f);
  low.AddRivals("rush", 30, 5000, 3);
  low.Submit("rush", 0);
  LeaderboardPage page;
  low.Load("rush", LeaderboardSpan::AllTime, 5, [&](const LeaderboardPage& p, bool) { page = p; });
  low.Update(0.0f);
  REQUIRE(page.player);
  CHECK((page.top.size() == 5 && page.player->rank == 31 && page.players == 31));

  // Offline: loads fail.
  boards.online = false;
  bool failed = false;
  boards.Load("classic", LeaderboardSpan::AllTime, 10,
              [&](const LeaderboardPage& p, bool ok) { failed = !ok && p.top.empty(); });
  boards.Update(1.0f);
  CHECK(failed);
}

TEST_CASE("fake leaderboards: signing in") {
  FakeLeaderboards boards(0.1f);
  boards.SignOut();
  bool failed = false;
  boards.Load("classic", LeaderboardSpan::AllTime, 10, [&](const LeaderboardPage&, bool ok) { failed = !ok; });
  boards.Update(0.2f);
  CHECK(failed);

  // Scores submitted while signed out wait for the sign-in; only the best is kept.
  boards.Submit("classic", 300);
  boards.Submit("classic", 200);
  boards.sign_in_fails = true;  // the player cancels
  boards.SignIn();
  CHECK(boards.State() == Leaderboards::SignInState::Checking);
  boards.Update(0.2f);
  CHECK(boards.State() == Leaderboards::SignInState::SignedOut);
  boards.sign_in_fails = false;
  boards.SignIn();
  boards.Update(0.2f);
  CHECK(boards.State() == Leaderboards::SignInState::SignedIn);
  LeaderboardPage page;
  boards.Load("classic", LeaderboardSpan::Week, 10, [&](const LeaderboardPage& p, bool) { page = p; });
  boards.Update(0.2f);
  REQUIRE(page.player);
  CHECK((page.player->score == 300 && page.players == 1 && page.player->name == "You"));
}

TEST_CASE("bridge leaderboards") {
  // No platform side on desktop: the bridge implementation starts signed out, and loads fail
  // through the message queue (not inside Load).
  auto boards = Leaderboards::CreateBridge();
  CHECK(boards->State() == Leaderboards::SignInState::SignedOut);
  bool failed = false;
  boards->Load("classic", LeaderboardSpan::AllTime, 10, [&](const LeaderboardPage&, bool ok) { failed = !ok; });
  CHECK(!failed);
  wlw::platform::DispatchBridgeMessages();
  CHECK(failed);

  // What the Kotlin service reports.
  wlw::platform::PostBridgeMessage("leaderboards", "state", R"({"signed_in": true})");
  wlw::platform::DispatchBridgeMessages();
  CHECK(boards->State() == Leaderboards::SignInState::SignedIn);

  // Answers carry the request's id (the second load: id 2). Here the test answers first; the
  // failure the missing platform side causes then finds no load waiting and is ignored.
  wlw::platform::PostBridgeMessage("leaderboards", "loaded", R"({"id": 2, "ok": true, "players": 1234,
      "top": [{"rank": 1, "score": 9000, "name": "Ada", "me": false},
              {"rank": 2, "score": 8000, "name": "Grace", "me": false}],
      "player": {"rank": 57, "score": 1500, "name": "Me", "me": true}})");
  LeaderboardPage page;
  bool ok = false;
  int answers = 0;
  boards->Load("classic", LeaderboardSpan::Today, 2, [&](const LeaderboardPage& p, bool result) {
    page = p;
    ok = result;
    ++answers;
  });
  wlw::platform::DispatchBridgeMessages();
  REQUIRE((ok && answers == 1));
  CHECK((page.players == 1234 && page.top.size() == 2));
  CHECK((page.top[0].name == "Ada" && page.top[0].score == 9000 && page.top[1].rank == 2));
  REQUIRE(page.player);
  CHECK((page.player->rank == 57 && page.player->score == 1500 && page.player->is_player));

  // Bad text is ignored, not a crash.
  wlw::platform::PostBridgeMessage("leaderboards", "loaded", "not json");
  wlw::platform::DispatchBridgeMessages();
  CHECK(answers == 1);

  // A player the service keeps off its public table (Play Games: a hidden profile): their score, rank 0.
  wlw::platform::PostBridgeMessage("leaderboards", "loaded",
                                   R"({"id": 3, "ok": true, "players": 0, "top": [],
                                       "player": {"rank": 0, "score": 7, "me": true}})");
  boards->Load("rush", LeaderboardSpan::AllTime, 10, [&](const LeaderboardPage& p, bool) { page = p; });
  wlw::platform::DispatchBridgeMessages();
  REQUIRE(page.player);
  CHECK((page.top.empty() && page.player->rank == 0 && page.player->score == 7 && page.players == 0));
}

TEST_SUITE_END();
