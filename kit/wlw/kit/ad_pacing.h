#pragma once

// When to show an interstitial: the rules that keep ads from driving players away.
// Plain logic with the clock passed in (seconds, any monotonic source), so it's testable.
//
// Every N finished games, never before the player has finished a few, never twice within a
// couple of minutes, and not right after the player chose to watch a rewarded ad (they just
// watched one). The game asks InterstitialDue() only at a natural break (between games), never
// mid-game.
namespace wlw::kit {

class AdPacing {
 public:
  struct Rules {
    int first_after_games = 3;           // the first games of a session are ad-free
    int games_between = 3;               // then one ad per this many finished games
    double min_seconds_between = 120.0;  // and never more often than this
  };

  AdPacing() = default;
  explicit AdPacing(const Rules& rules) : rules(rules) {}

  void GameFinished() {
    ++games_finished_;
    ++games_since_ad_;
  }
  // Watching a rewarded ad counts as having seen an ad.
  void RewardedWatched(double now) { AdShown(now); }
  void InterstitialShown(double now) { AdShown(now); }

  bool InterstitialDue(double now) const {
    if (games_finished_ < rules.first_after_games) return false;
    if (games_since_ad_ < rules.games_between) return false;
    return !any_ad_ || now - last_ad_time_ >= rules.min_seconds_between;
  }

  int GamesFinished() const { return games_finished_; }
  int GamesSinceAd() const { return games_since_ad_; }

  Rules rules;

 private:
  void AdShown(double now) {
    games_since_ad_ = 0;
    last_ad_time_ = now;
    any_ad_ = true;
  }

  int games_finished_ = 0;
  int games_since_ad_ = 0;
  double last_ad_time_ = 0.0;
  bool any_ad_ = false;
};

}  // namespace wlw::kit
