#pragma once

#include <functional>
#include <memory>
#include <string>

// Ads for games: rewarded ads (the player chooses to watch one for a reward, e.g. "continue")
// and interstitials (a full-screen ad between games).
//
// Ads::Create picks the implementation:
//   Android: AdMob, through the native bridge (platform/bridge.h) to the Kotlin service
//            com.wlw.kit.AdMobService (kit/android/), which also asks for consent where the law
//            requires it (Google's UMP: GDPR in the EU/UK, US state privacy laws).
//   desktop: FakeAds - an "ad" that plays for a couple of seconds, so every flow can be tried.
//
// All callbacks run on the game thread. Ads load in the background; Show* only works when
// *Ready() says so (otherwise the callback reports failure at once).
namespace wlw::kit {

struct AdsConfig {
  // AdMob ad unit ids. The defaults are Google's official TEST units: they always serve test
  // ads and are safe to click. Real ids come from the AdMob console; never test with them
  // (clicking your own real ads gets the account banned).
  std::string rewarded_unit = "ca-app-pub-3940256099942544/5224354917";
  std::string interstitial_unit = "ca-app-pub-3940256099942544/1033173712";
  float fake_seconds = 2.0f;  // how long a desktop fake ad "plays"
};

class Ads {
 public:
  using Config = AdsConfig;

  static std::unique_ptr<Ads> Create(const Config& config = {});
  virtual ~Ads() = default;

  virtual void Update(float dt) {}  // call once per frame (the fake uses it to play)

  virtual bool RewardedReady() const = 0;
  virtual bool InterstitialReady() const = 0;
  // `done(rewarded)`: after the ad closed; rewarded = the player watched long enough to earn it.
  virtual void ShowRewarded(std::function<void(bool rewarded)> done) = 0;
  virtual void ShowInterstitial(std::function<void()> done) = 0;
  // An ad is on screen (on desktop: the fake one; the game draws it, see FakeAds).
  virtual bool Showing() const = 0;

  // Where a privacy law applies, players must be able to change their consent later: show a
  // "Privacy" button (in the menu or settings) when this is true.
  virtual bool PrivacyOptionsRequired() const = 0;
  virtual void ShowPrivacyOptions() = 0;

  virtual std::string Describe() const = 0;  // one line for debug tools
};

// The desktop stand-in. While Showing(), the game draws something in its place (a dark screen
// with "AD" and Progress()) and doesn't take input; the game loop keeps running underneath, as
// it does on Android while the real ad covers it.
class FakeAds : public Ads {
 public:
  explicit FakeAds(float seconds) : seconds_(seconds) {}
  void Update(float dt) override;
  bool RewardedReady() const override { return available; }
  bool InterstitialReady() const override { return available; }
  void ShowRewarded(std::function<void(bool rewarded)> done) override;
  void ShowInterstitial(std::function<void()> done) override;
  bool Showing() const override { return showing_; }
  bool PrivacyOptionsRequired() const override { return privacy_required; }
  void ShowPrivacyOptions() override {}
  std::string Describe() const override;

  bool ShowingRewarded() const { return showing_ && rewarded_done_ != nullptr; }
  float Progress() const { return seconds_ > 0.0f ? elapsed_ / seconds_ : 1.0f; }  // 0..1
  void Skip() { skipped_ = true; }  // close early: a rewarded ad then gives no reward

  // Knobs for trying the game's other paths (debug panel, tests).
  bool available = true;          // false = "no fill": nothing ready, as with no network
  bool privacy_required = false;  // as for a player in the EU

 private:
  float seconds_;
  bool showing_ = false;
  bool skipped_ = false;
  float elapsed_ = 0.0f;
  std::function<void(bool)> rewarded_done_;
  std::function<void()> interstitial_done_;
};

}  // namespace wlw::kit
