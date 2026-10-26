#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Global leaderboards: every player's best score on a shared table kept by the platform's game
// service, ranked by its servers (the game never sorts other people's scores itself).
//
// Leaderboards::Create picks the implementation:
//   Android: Google Play Games Services v2, through the native bridge (platform/bridge.h) to the
//            Kotlin service com.wlw.kit.PlayGamesService (kit/android/playgames/).
//   desktop: FakeLeaderboards - made-up rivals in memory, so every screen state can be tried.
// (iOS later: Game Center behind the same interface.)
//
// The service keeps ONE best score per player and time span: submitting a lower score changes
// nothing, so a game can submit every finished game. The daily and weekly spans reset on the
// service's clock (Play Games: US Pacific time), not the player's.
//
// Everything is asynchronous (network). Callbacks run on the game thread during a later frame -
// never inside the call that started them - and may arrive after the screen that asked is gone:
// capture something that outlives it.
//
// Scores come from the player's device, so a modified game can submit anything. Set limits per
// leaderboard in the service's console (Play Console: min/max score, tamper protection) and
// don't submit scores the game knows are fake (debug tools).
namespace wlw::kit {

enum class LeaderboardSpan { Today, Week, AllTime };

struct LeaderboardEntry {
  int64_t rank = 0;  // 1 = the best; 0 = unknown (the player has a score but no public rank, see `player`)
  int64_t score = 0;
  std::string name;        // the player's public name, UTF-8 (any script)
  bool is_player = false;  // the signed-in player's own entry
};

// One look at a leaderboard: the best entries and where the signed-in player stands.
struct LeaderboardPage {
  std::vector<LeaderboardEntry> top;  // best first
  // The signed-in player's entry; none: no score in this span yet. Its rank is 0 when the service
  // keeps them off public tables (Play Games: a profile that isn't visible to everyone).
  std::optional<LeaderboardEntry> player;
  int64_t players = -1;  // players with a score in this span (approximate); -1 = unknown
};

class Leaderboards {
 public:
  enum class SignInState {
    Checking,   // asking the platform (at start, or after SignIn())
    SignedIn,   //
    SignedOut,  // no account, cancelled, or not set up: show a "Sign in" button
  };

  static std::unique_ptr<Leaderboards> Create();
  // The bridge implementation whatever the platform (Create picks it on Android). Without a
  // platform side, every load fails; tests drive it with platform::PostBridgeMessage.
  static std::unique_ptr<Leaderboards> CreateBridge();
  virtual ~Leaderboards() = default;

  virtual void Update(float dt) {}  // call once per frame (the fake uses it to answer)

  // Platforms sign the player in silently at start; SignIn() is for a button the player taps
  // (it may show the platform's own sign-in screen).
  virtual SignInState State() const = 0;
  virtual void SignIn() = 0;

  // `board` is the leaderboard's id in the service's console. Fire and forget: the platform
  // queues it and retries when offline. While signed out, the best score per board is kept and
  // sent at sign-in.
  virtual void Submit(const std::string& board, int64_t score) = 0;

  // The best `count` entries (1-25) of `board` in `span`, plus the player's own entry.
  // `ok` = false: not signed in (yet), offline, or no such board (the page is then empty).
  // After a Submit to `board`, the next load skips the platform's cache.
  using LoadDone = std::function<void(const LeaderboardPage& page, bool ok)>;
  virtual void Load(const std::string& board, LeaderboardSpan span, int count, LoadDone done) = 0;

  virtual std::string Describe() const = 0;  // one line for debug tools
};

// The desktop stand-in: a service in memory. Starts signed in; answers after `delay` seconds
// (in Update). Every span shows the same scores, with fewer rivals in shorter spans, and nothing
// ever resets.
class FakeLeaderboards : public Leaderboards {
 public:
  explicit FakeLeaderboards(float delay = 0.4f) : delay_(delay) {}

  void Update(float dt) override;
  SignInState State() const override { return state_; }
  void SignIn() override;
  void Submit(const std::string& board, int64_t score) override;
  void Load(const std::string& board, LeaderboardSpan span, int count, LoadDone done) override;
  std::string Describe() const override;

  // Knobs for trying the game's other paths (debug panel, tests).
  bool online = true;          // false: every load fails, as with no network
  bool sign_in_fails = false;  // SignIn() ends signed out, as when the player cancels
  bool public_profile = true;  // false: the player is left off the tables (rank 0), as with a hidden Play Games profile
  std::string player_name = "You";
  void SignOut() { state_ = SignInState::SignedOut; }
  // Adds `count` made-up rivals ("SwiftOtter"...) to `board` with scores up to `max_score`, the
  // same ones for the same seed. About half of them also played this week, a fifth today.
  void AddRivals(const std::string& board, int count, int64_t max_score, uint64_t seed = 1);

 private:
  struct Score {
    std::string name;
    int64_t score = 0;
    int spans = 0;  // bit per LeaderboardSpan it counts in
    bool player = false;
  };
  struct Pending {
    float wait = 0.0f;
    std::function<void()> answer;
  };
  void Later(std::function<void()> answer);
  void Record(const std::string& board, int64_t score);
  LeaderboardPage Page(const std::string& board, LeaderboardSpan span, int count) const;

  float delay_;
  SignInState state_ = SignInState::SignedIn;
  std::map<std::string, std::vector<Score>> boards_;
  std::map<std::string, int64_t> unsent_;  // best per board submitted while signed out
  std::vector<Pending> pending_;
};

}  // namespace wlw::kit
