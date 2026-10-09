// Tests for small engine building blocks: core::Random, the platform's save-file functions and the
// native bridge's message queue.
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "wlw/core/random.h"
#include "wlw/platform/bridge.h"
#include "wlw/platform/platform.h"

using wlw::core::Random;

static void TestRandomIsPinned() {
  // These exact numbers must never change: seeded levels, daily puzzles and saved games depend on
  // the sequence (the same values are pinned in Bluzzle's tests).
  Random rng(0);
  CHECK(rng.Next() == 0xE220A839u);
  CHECK(rng.Next() == 0x6E789E6Au);
}

static void TestRandomRangesAndState() {
  Random rng(123);
  for (int i = 0; i < 1000; ++i) {
    const int b = rng.Below(7);
    CHECK(b >= 0 && b < 7);
    const float u = rng.Unit();
    CHECK(u >= 0.0f && u < 1.0f);
    const float r = rng.Range(-2.0f, 3.0f);
    CHECK(r >= -2.0f && r < 3.0f);
  }
  CHECK(rng.Below(0) == 0);
  // Saving and restoring the state continues the exact same sequence (resuming a game).
  const uint64_t saved = rng.State();
  const uint32_t a = rng.Next(), b = rng.Next();
  Random resumed;
  resumed.SetState(saved);
  CHECK(resumed.Next() == a && resumed.Next() == b);
}

static void TestAtomicWriteAndRead() {
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

static void TestBridgeQueue() {
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
  CHECK(seen.size() == 2 && seen[0] == "a1" && seen[1] == "b2");

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

int main() {
  TestRandomIsPinned();
  TestRandomRangesAndState();
  TestAtomicWriteAndRead();
  TestBridgeQueue();
  return TestResult("core");
}
