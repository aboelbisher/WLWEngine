#include "wlw/kit/leaderboards.h"

#include <algorithm>
#include <utility>

#include "json.hpp"
#include "wlw/core/log.h"
#include "wlw/core/random.h"
#include "wlw/kit/kit_log.h"
#include "wlw/platform/bridge.h"

namespace wlw::kit {

namespace {

using Json = nlohmann::json;

constexpr const char* kService = "leaderboards";

const char* SpanName(LeaderboardSpan span) {
  switch (span) {
    case LeaderboardSpan::Today: return "today";
    case LeaderboardSpan::Week: return "week";
    case LeaderboardSpan::AllTime: return "all_time";
  }
  return "all_time";
}

LeaderboardEntry EntryFromJson(const Json& json) {
  LeaderboardEntry entry;
  entry.rank = json.value("rank", int64_t{0});
  entry.score = json.value("score", int64_t{0});
  entry.name = json.value("name", std::string());
  entry.is_player = json.value("me", false);
  return entry;
}

// Play Games through the native bridge (PlayGamesService.kt). This side keeps the sign-in state
// the Kotlin service reports, the scores waiting for a sign-in, and the callbacks of the loads in
// flight (by request id: the player may switch tabs before the first answer arrives).
//
// Calls:  init, sign_in, submit {board, score}, load {id, board, span, count}
// Events: state {signed_in}, loaded {id, ok, top: [{rank, score, name, me}], player, players},
//         error {method, message}
class BridgeLeaderboards : public Leaderboards {
 public:
  BridgeLeaderboards() {
    platform::SetBridgeHandler(kService,
                               [this](const std::string& event, const std::string& json) { OnEvent(event, json); });
    if (!platform::BridgeCall(kService, "init")) state_ = SignInState::SignedOut;
  }
  ~BridgeLeaderboards() override { platform::SetBridgeHandler(kService, nullptr); }

  SignInState State() const override { return state_; }

  void SignIn() override {
    if (state_ == SignInState::SignedIn) return;
    state_ = platform::BridgeCall(kService, "sign_in") ? SignInState::Checking : SignInState::SignedOut;
  }

  void Submit(const std::string& board, int64_t score) override {
    if (score < 0) return;
    if (state_ != SignInState::SignedIn) {
      int64_t& best = unsent_[board];
      best = std::max(best, score);
      return;
    }
    platform::BridgeCall(kService, "submit", Json{{"board", board}, {"score", score}}.dump());
  }

  void Load(const std::string& board, LeaderboardSpan span, int count, LoadDone done) override {
    const int id = next_id_++;
    loads_[id] = std::move(done);
    const Json args = {{"id", id}, {"board", board}, {"span", SpanName(span)}, {"count", std::clamp(count, 1, 25)}};
    if (state_ != SignInState::SignedIn || !platform::BridgeCall(kService, "load", args.dump())) {
      // Answer through the queue like any other result: never inside Load.
      platform::PostBridgeMessage(kService, "loaded", Json{{"id", id}, {"ok", false}}.dump());
    }
  }

  std::string Describe() const override {
    const char* state = state_ == SignInState::SignedIn   ? "signed in"
                        : state_ == SignInState::Checking ? "checking sign-in"
                                                          : "signed out";
    return std::string("Play Games leaderboards: ") + state + ", " + std::to_string(loads_.size()) + " loading, " +
           std::to_string(unsent_.size()) + " boards waiting to submit";
  }

 private:
  void OnEvent(const std::string& event, const std::string& text) {
    const Json json = Json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!json.is_object()) {
      WLW_LOG(LogKit, Warning, "Leaderboards: bad %s message: %s", event.c_str(), text.c_str());
      return;
    }
    if (event == "state") {
      const bool signed_in = json.value("signed_in", false);
      state_ = signed_in ? SignInState::SignedIn : SignInState::SignedOut;
      if (signed_in) SendUnsent();
    } else if (event == "loaded") {
      const auto it = loads_.find(json.value("id", -1));
      if (it == loads_.end()) return;
      LoadDone done = std::move(it->second);
      loads_.erase(it);
      const bool ok = json.value("ok", false);
      LeaderboardPage page;
      if (ok) {
        for (const Json& entry : json.value("top", Json::array())) page.top.push_back(EntryFromJson(entry));
        if (json.contains("player") && json["player"].is_object()) page.player = EntryFromJson(json["player"]);
        page.players = json.value("players", int64_t{-1});
      }
      if (done) done(page, ok);
    } else if (event == "error") {
      WLW_LOG(LogKit, Warning, "Leaderboards: %s", text.c_str());
      const std::string method = json.value("method", std::string());
      if (method == "init" || method == "sign_in") state_ = SignInState::SignedOut;
      // A load that failed before it could answer (no such service...): fail every load in
      // flight, the game asks again.
      if (method == "load") FailAllLoads();
    }
  }

  void SendUnsent() {
    for (const auto& [board, score] : unsent_) {
      platform::BridgeCall(kService, "submit", Json{{"board", board}, {"score", score}}.dump());
    }
    unsent_.clear();
  }

  void FailAllLoads() {
    auto loads = std::move(loads_);
    loads_.clear();
    for (auto& [id, done] : loads) {
      if (done) done(LeaderboardPage{}, false);
    }
  }

  SignInState state_ = SignInState::Checking;
  std::map<std::string, int64_t> unsent_;
  std::map<int, LoadDone> loads_;
  int next_id_ = 1;
};

constexpr int SpanBit(LeaderboardSpan span) { return 1 << static_cast<int>(span); }
constexpr int kAllSpans = 7;

}  // namespace

std::unique_ptr<Leaderboards> Leaderboards::Create() {
  if (platform::HasBridge()) return CreateBridge();
  return std::make_unique<FakeLeaderboards>();
}

std::unique_ptr<Leaderboards> Leaderboards::CreateBridge() { return std::make_unique<BridgeLeaderboards>(); }

// ---- FakeLeaderboards ----

void FakeLeaderboards::Update(float dt) {
  // Answers may queue more answers (a callback that loads again): take the due ones out first.
  std::vector<std::function<void()>> due;
  for (Pending& pending : pending_) {
    pending.wait -= dt;
    if (pending.wait <= 0.0f) due.push_back(std::move(pending.answer));
  }
  std::erase_if(pending_, [](const Pending& pending) { return pending.wait <= 0.0f; });
  for (auto& answer : due) answer();
}

void FakeLeaderboards::SignIn() {
  if (state_ == SignInState::SignedIn) return;
  state_ = SignInState::Checking;
  Later([this] {
    state_ = sign_in_fails ? SignInState::SignedOut : SignInState::SignedIn;
    if (state_ != SignInState::SignedIn) return;
    for (const auto& [board, score] : unsent_) Record(board, score);
    unsent_.clear();
  });
}

void FakeLeaderboards::Submit(const std::string& board, int64_t score) {
  if (score < 0) return;
  if (state_ != SignInState::SignedIn) {
    int64_t& best = unsent_[board];
    best = std::max(best, score);
    return;
  }
  Record(board, score);
}

void FakeLeaderboards::Load(const std::string& board, LeaderboardSpan span, int count, LoadDone done) {
  Later([this, board, span, count, done = std::move(done)] {
    const bool ok = online && state_ == SignInState::SignedIn;
    if (done) done(ok ? Page(board, span, std::clamp(count, 1, 25)) : LeaderboardPage{}, ok);
  });
}

std::string FakeLeaderboards::Describe() const {
  const char* state = state_ == SignInState::SignedIn   ? "signed in"
                      : state_ == SignInState::Checking ? "checking sign-in"
                                                        : "signed out";
  size_t scores = 0;
  for (const auto& [board, entries] : boards_) scores += entries.size();
  return std::string("Fake leaderboards: ") + state + (online ? "" : ", offline") + ", " +
         std::to_string(boards_.size()) + " boards, " + std::to_string(scores) + " scores";
}

void FakeLeaderboards::AddRivals(const std::string& board, int count, int64_t max_score, uint64_t seed) {
  core::Random random(seed);
  std::vector<Score>& scores = boards_[board];
  for (int i = 0; i < count; ++i) {
    Score rival;
    rival.name = "Player " + std::to_string(i + 1);
    // Squared: most rivals score low, a few high, as on real boards.
    const float unit = random.Unit();
    rival.score = static_cast<int64_t>(static_cast<double>(unit) * unit * static_cast<double>(max_score));
    rival.spans = SpanBit(LeaderboardSpan::AllTime);
    if (random.Chance(0.5f)) rival.spans |= SpanBit(LeaderboardSpan::Week);
    if ((rival.spans & SpanBit(LeaderboardSpan::Week)) && random.Chance(0.4f))
      rival.spans |= SpanBit(LeaderboardSpan::Today);
    scores.push_back(rival);
  }
}

void FakeLeaderboards::Later(std::function<void()> answer) { pending_.push_back({delay_, std::move(answer)}); }

void FakeLeaderboards::Record(const std::string& board, int64_t score) {
  std::vector<Score>& scores = boards_[board];
  const auto mine = std::find_if(scores.begin(), scores.end(), [](const Score& s) { return s.player; });
  if (mine == scores.end()) {
    scores.push_back({player_name, score, kAllSpans, true});
  } else {
    mine->score = std::max(mine->score, score);  // the service keeps the best
    mine->spans = kAllSpans;                     // played just now: counts in every span
  }
}

LeaderboardPage FakeLeaderboards::Page(const std::string& board, LeaderboardSpan span, int count) const {
  std::vector<const Score*> ranked;
  const Score* hidden = nullptr;  // the player, left off the public table
  if (const auto it = boards_.find(board); it != boards_.end()) {
    for (const Score& score : it->second) {
      if (!(score.spans & SpanBit(span))) continue;
      if (score.player && !public_profile) {
        hidden = &score;
      } else {
        ranked.push_back(&score);
      }
    }
  }
  // Best first; equal scores keep the order they arrived in.
  std::stable_sort(ranked.begin(), ranked.end(), [](const Score* a, const Score* b) { return a->score > b->score; });
  LeaderboardPage page;
  page.players = static_cast<int64_t>(ranked.size());
  for (int i = 0; i < static_cast<int>(ranked.size()); ++i) {
    const LeaderboardEntry entry{i + 1, ranked[i]->score, ranked[i]->name, ranked[i]->player};
    if (i < count) page.top.push_back(entry);
    if (entry.is_player) page.player = entry;
  }
  if (hidden) page.player = LeaderboardEntry{0, hidden->score, hidden->name, true};
  return page;
}

}  // namespace wlw::kit
