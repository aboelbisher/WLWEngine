#include "wlw/kit/ads.h"

#include <utility>

#include "json.hpp"
#include "wlw/core/log.h"
#include "wlw/kit/kit_log.h"
#include "wlw/platform/bridge.h"

namespace wlw::kit {

namespace {

using Json = nlohmann::json;

// AdMob through the native bridge. This side only keeps the state the Kotlin service reports
// ("state" events) and the callback of the ad being shown.
class BridgeAds : public Ads {
 public:
  explicit BridgeAds(const Config& config) {
    platform::SetBridgeHandler("ads",
                               [this](const std::string& event, const std::string& json) { OnEvent(event, json); });
    const Json args = {{"rewarded", config.rewarded_unit},
                       {"interstitial", config.interstitial_unit},
                       {"test_devices", config.test_devices},
                       {"debug_consent_eea", config.debug_consent_eea}};
    platform::BridgeCall("ads", "init", args.dump());
  }
  ~BridgeAds() override { platform::SetBridgeHandler("ads", nullptr); }

  bool RewardedReady() const override { return rewarded_ready_ && !Showing(); }
  bool InterstitialReady() const override { return interstitial_ready_ && !Showing(); }

  void ShowRewarded(std::function<void(bool rewarded)> done) override {
    if (!RewardedReady() || !platform::BridgeCall("ads", "show_rewarded")) {
      if (done) done(false);
      return;
    }
    rewarded_done_ = std::move(done);
    rewarded_ready_ = false;
  }

  void ShowInterstitial(std::function<void()> done) override {
    if (!InterstitialReady() || !platform::BridgeCall("ads", "show_interstitial")) {
      if (done) done();
      return;
    }
    interstitial_done_ = std::move(done);
    interstitial_ready_ = false;
  }

  bool Showing() const override { return rewarded_done_ != nullptr || interstitial_done_ != nullptr; }
  bool PrivacyOptionsRequired() const override { return privacy_required_; }
  void ShowPrivacyOptions() override { platform::BridgeCall("ads", "privacy_options"); }
  void ResetConsent() override { platform::BridgeCall("ads", "reset_consent"); }

  std::string Describe() const override {
    return std::string("AdMob: rewarded ") + (rewarded_ready_ ? "ready" : "loading") + ", interstitial " +
           (interstitial_ready_ ? "ready" : "loading") + (Showing() ? ", SHOWING" : "") +
           (privacy_required_ ? ", privacy options required" : "");
  }

 private:
  void OnEvent(const std::string& event, const std::string& text) {
    const Json json = Json::parse(text, nullptr, /*allow_exceptions=*/false);
    const auto flag = [&](const char* key) { return json.is_object() && json.value(key, false); };
    if (event == "state") {
      rewarded_ready_ = flag("rewarded_ready");
      interstitial_ready_ = flag("interstitial_ready");
      privacy_required_ = flag("privacy_required");
    } else if (event == "rewarded_done") {
      Finish(rewarded_done_, flag("rewarded"));
    } else if (event == "interstitial_done") {
      Finish(interstitial_done_);
    } else if (event == "error") {
      WLW_LOG(LogKit, Warning, "Ads: %s", text.c_str());
      // A show that failed on the Kotlin side still has to answer the game.
      const std::string method = json.is_object() ? json.value("method", std::string()) : std::string();
      if (method == "show_rewarded") Finish(rewarded_done_, false);
      if (method == "show_interstitial") Finish(interstitial_done_);
    }
  }

  // Clears the stored callback before calling it: the callback may show the next ad.
  static void Finish(std::function<void(bool)>& stored, bool rewarded) {
    auto done = std::move(stored);
    stored = nullptr;
    if (done) done(rewarded);
  }
  static void Finish(std::function<void()>& stored) {
    auto done = std::move(stored);
    stored = nullptr;
    if (done) done();
  }

  bool rewarded_ready_ = false;
  bool interstitial_ready_ = false;
  bool privacy_required_ = false;
  std::function<void(bool)> rewarded_done_;
  std::function<void()> interstitial_done_;
};

}  // namespace

std::unique_ptr<Ads> Ads::Create(const Config& config) {
  if (platform::HasBridge()) return std::make_unique<BridgeAds>(config);
  return std::make_unique<FakeAds>(config.fake_seconds);
}

// ---- FakeAds ----

void FakeAds::ShowRewarded(std::function<void(bool rewarded)> done) {
  if (showing_ || !available) {
    if (done) done(false);
    return;
  }
  showing_ = true;
  skipped_ = false;
  elapsed_ = 0.0f;
  rewarded_done_ = done ? std::move(done) : [](bool) {};
}

void FakeAds::ShowInterstitial(std::function<void()> done) {
  if (showing_ || !available) {
    if (done) done();
    return;
  }
  showing_ = true;
  skipped_ = false;
  elapsed_ = 0.0f;
  interstitial_done_ = done ? std::move(done) : [] {};
}

void FakeAds::Update(float dt) {
  if (!showing_) return;
  elapsed_ += dt;
  if (elapsed_ < seconds_ && !skipped_) return;
  showing_ = false;
  const bool rewarded = !skipped_;
  auto rewarded_done = std::move(rewarded_done_);
  auto interstitial_done = std::move(interstitial_done_);
  rewarded_done_ = nullptr;
  interstitial_done_ = nullptr;
  if (rewarded_done) rewarded_done(rewarded);
  if (interstitial_done) interstitial_done();
}

std::string FakeAds::Describe() const {
  return std::string("Fake ads (desktop): ") + (available ? "available" : "no fill") +
         (showing_ ? (rewarded_done_ ? ", showing rewarded" : ", showing interstitial") : "");
}

}  // namespace wlw::kit
