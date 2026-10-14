// Tests for small engine building blocks: core::Random, the platform's save-file functions and the
// native bridge's message queue.
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "test_helpers.h"
#include "wlw/core/random.h"
#include "wlw/core/time_scale.h"
#include "wlw/platform/bridge.h"
#include "wlw/platform/platform.h"

TEST_SUITE_BEGIN("core");

using wlw::core::Random;

TEST_CASE("random is pinned") {
  // These exact numbers must never change: seeded levels, daily puzzles and saved games depend on
  // the sequence (the same values are pinned in Bluzzle's tests).
  Random rng(0);
  CHECK(rng.Next() == 0xE220A839u);
  CHECK(rng.Next() == 0x6E789E6Au);
}

TEST_CASE("random ranges and state") {
  Random rng(123);
  for (int i = 0; i < 1000; ++i) {
    const int b = rng.Below(7);
    CHECK((b >= 0 && b < 7));
    const float u = rng.Unit();
    CHECK((u >= 0.0f && u < 1.0f));
    const float r = rng.Range(-2.0f, 3.0f);
    CHECK((r >= -2.0f && r < 3.0f));
  }
  CHECK(rng.Below(0) == 0);
  // Saving and restoring the state continues the exact same sequence (resuming a game).
  const uint64_t saved = rng.State();
  const uint32_t a = rng.Next(), b = rng.Next();
  Random resumed;
  resumed.SetState(saved);
  CHECK((resumed.Next() == a && resumed.Next() == b));
}

TEST_CASE("atomic write and read") {
  namespace fs = std::filesystem;
  const fs::path folder = fs::temp_directory_path() / "wlw_core_tests";
  fs::create_directories(folder);
  const std::string path = (folder / "save.json").string();

  const std::vector<uint8_t> first = {'{', '1', '}'};
  CHECK(wlw::platform::WriteFileAtomic(path, first));
  CHECK(wlw::platform::ReadLocalFile(path) == first);
  const std::vector<uint8_t> second(10000, 'x');  // replacing an existing file
  CHECK(wlw::platform::WriteFileAtomic(path, second));
  CHECK(wlw::platform::ReadLocalFile(path) == second);
  CHECK(!fs::exists(path + ".tmp"));  // the temp file is renamed, not left behind
  CHECK(!wlw::platform::ReadLocalFile((folder / "missing.json").string()).has_value());
  CHECK(!wlw::platform::WriteFileAtomic((folder / "no" / "such" / "dir.json").string(), first));
  fs::remove_all(folder);
}

TEST_CASE("bridge queue") {
  using namespace wlw::platform;
  CHECK(!HasBridge());  // desktop: no platform-language side
  CHECK(!BridgeCall("ads", "init"));

  std::vector<std::string> seen;
  SetBridgeHandler("test", [&](const std::string& event, const std::string& json) { seen.push_back(event + json); });
  // Posted from other threads (as Android's UI thread does), delivered in order on this one.
  std::thread first([] { PostBridgeMessage("test", "a", "1"); });
  first.join();
  std::thread second([] { PostBridgeMessage("test", "b", "2"); });
  second.join();
  PostBridgeMessage("nobody", "x", "{}");  // no handler: dropped
  CHECK(seen.empty());                     // nothing runs before the dispatch
  CHECK(DispatchBridgeMessages() == 3);
  CHECK((seen.size() == 2 && seen[0] == "a1" && seen[1] == "b2"));

  // A handler that posts again: delivered at the next dispatch, not in a loop.
  SetBridgeHandler("test", [&](const std::string& event, const std::string&) {
    seen.push_back(event);
    if (event == "ping") PostBridgeMessage("test", "pong", "");
  });
  PostBridgeMessage("test", "ping", "");
  CHECK(DispatchBridgeMessages() == 1);
  CHECK(DispatchBridgeMessages() == 1);
  CHECK(seen.back() == "pong");
  SetBridgeHandler("test", nullptr);
  CHECK(DispatchBridgeMessages() == 0);
}

TEST_CASE("time scale") {
  wlw::core::TimeScale time;
  CHECK((Near(time.Apply(0.016f), 0.016f) && time.Normal()));  // normal speed by default

  // Hit-stop: frozen for 60 ms of real time. The frame that crosses the end is split.
  time.Set(0.0f, 0.06f);
  CHECK(Near(time.Apply(0.05f), 0.0f));
  CHECK(Near(time.Apply(0.02f), 0.01f));  // 10 ms frozen, then 10 ms at normal speed
  CHECK((time.Normal() && Near(time.Apply(0.016f), 0.016f)));

  // Slow motion with an eased recovery: 0.5 for 0.2 s, then linearly back to 1 over 0.2 s.
  time.Set(0.5f, 0.2f, 0.2f);
  CHECK(Near(time.Apply(0.2f), 0.1f));
  CHECK(Near(time.Scale(), 0.5f));
  CHECK(Near(time.Apply(0.1f), 0.1f * (0.5f + 0.75f) / 2.0f));  // halfway: the average of 0.5 and 0.75
  CHECK(Near(time.Scale(), 0.75f));
  CHECK(Near(time.Apply(0.3f), 0.1f * (0.75f + 1.0f) / 2.0f + 0.2f));  // the rest of the ramp, then normal
  CHECK(time.Normal());

  // Held until told otherwise; a zero hold goes straight to the recovery.
  time.Set(0.25f);
  for (int i = 0; i < 100; ++i) time.Apply(0.1f);
  CHECK(Near(time.Apply(0.1f), 0.025f));
  time.Set(0.0f, 0.0f, 0.1f);
  CHECK((Near(time.Apply(0.1f), 0.05f) && time.Normal()));
  time.Set(0.3f, 0.0f);
  CHECK(time.Normal());
}

TEST_SUITE_END();
