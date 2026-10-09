package com.wlw.kit

import android.app.Activity
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.google.android.gms.ads.AdError
import com.google.android.gms.ads.AdRequest
import com.google.android.gms.ads.FullScreenContentCallback
import com.google.android.gms.ads.LoadAdError
import com.google.android.gms.ads.MobileAds
import com.google.android.gms.ads.RequestConfiguration
import com.google.android.gms.ads.interstitial.InterstitialAd
import com.google.android.gms.ads.interstitial.InterstitialAdLoadCallback
import com.google.android.gms.ads.rewarded.RewardedAd
import com.google.android.gms.ads.rewarded.RewardedAdLoadCallback
import com.google.android.ump.ConsentDebugSettings
import com.google.android.ump.ConsentInformation
import com.google.android.ump.ConsentRequestParameters
import com.google.android.ump.UserMessagingPlatform
import com.wlw.WlwBridge
import com.wlw.WlwService
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicBoolean

// The "ads" service of the native bridge: Google AdMob + Google's consent form (UMP), for
// wlw::kit::Ads (kit/wlw/kit/ads.h). Everything here runs on the UI thread.
//
// Calls from C++:  init {rewarded, interstitial, test_devices, debug_consent_eea}, show_rewarded,
//                  show_interstitial, privacy_options, reset_consent
// Events to C++:   state {rewarded_ready, interstitial_ready, privacy_required},
//                  rewarded_done {rewarded}, interstitial_done, error {method, message}
//
// The game's manifest must name its AdMob app id:
//   <meta-data android:name="com.google.android.gms.ads.APPLICATION_ID" android:value="ca-app-pub-...~..."/>
class AdMobService(private val activity: Activity) : WlwService {
    override val name = "ads"

    private val handler = Handler(Looper.getMainLooper())
    private var consent: ConsentInformation? = null
    private val started = AtomicBoolean(false)
    private var rewardedUnit = ""
    private var interstitialUnit = ""

    private var rewarded: RewardedAd? = null
    private var interstitial: InterstitialAd? = null
    private var loadingRewarded = false
    private var loadingInterstitial = false
    private var rewardedRetry = 0     // failed loads in a row: the wait before the next try doubles
    private var interstitialRetry = 0
    private var destroyed = false

    override fun onCall(method: String, args: JSONObject) {
        when (method) {
            "init" -> init(args)
            "show_rewarded" -> showRewarded()
            "show_interstitial" -> showInterstitial()
            "privacy_options" -> UserMessagingPlatform.showPrivacyOptionsForm(activity) { sendState() }
            "reset_consent" -> consent?.reset()  // debug: the form shows again at the next start
            else -> reportError(method, "unknown method")
        }
    }

    override fun onDestroy() {
        destroyed = true
        handler.removeCallbacksAndMessages(null)
    }

    // Consent first: at every start, ask Google whether a consent form is needed here (EU/UK...),
    // show it if so, and only then load ads. A previous session's answer allows starting at once.
    private fun init(args: JSONObject) {
        rewardedUnit = args.optString("rewarded")
        interstitialUnit = args.optString("interstitial")
        val testDevices = mutableListOf<String>()
        args.optJSONArray("test_devices")?.let { list ->
            for (i in 0 until list.length()) testDevices.add(list.getString(i))
        }
        // The developer's own devices get test ads even from real ad units.
        MobileAds.setRequestConfiguration(RequestConfiguration.Builder().setTestDeviceIds(testDevices).build())

        val params = ConsentRequestParameters.Builder()
        if (testDevices.isNotEmpty() || args.optBoolean("debug_consent_eea")) {
            val debug = ConsentDebugSettings.Builder(activity)
            for (id in testDevices) debug.addTestDeviceHashedId(id)
            if (args.optBoolean("debug_consent_eea")) {
                debug.setDebugGeography(ConsentDebugSettings.DebugGeography.DEBUG_GEOGRAPHY_EEA)
            }
            params.setConsentDebugSettings(debug.build())
        }
        val info = UserMessagingPlatform.getConsentInformation(activity)
        consent = info
        info.requestConsentInfoUpdate(
            activity,
            params.build(),
            {
                Log.i(TAG, "Consent: status ${info.consentStatus} (1 not required, 2 required, 3 obtained), " +
                    "form available ${info.isConsentFormAvailable}, privacy options ${info.privacyOptionsRequirementStatus}")
                UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity) { formError ->
                    if (formError != null) Log.w(TAG, "Consent form: ${formError.message}")
                    startAds()
                }
            },
            { requestError ->
                Log.w(TAG, "Consent info: ${requestError.message}")
                startAds()
            })
        if (info.canRequestAds()) startAds()
    }

    private fun startAds() {
        sendState()
        if (consent?.canRequestAds() != true || !started.compareAndSet(false, true)) return
        // Google recommends initializing off the UI thread; loading starts when it's done.
        Thread {
            MobileAds.initialize(activity) {
                activity.runOnUiThread {
                    loadRewarded()
                    loadInterstitial()
                }
            }
        }.start()
    }

    // ---- Rewarded ----

    private fun loadRewarded() {
        if (destroyed || loadingRewarded || rewarded != null || rewardedUnit.isEmpty()) return
        loadingRewarded = true
        RewardedAd.load(activity, rewardedUnit, AdRequest.Builder().build(), object : RewardedAdLoadCallback() {
            override fun onAdLoaded(ad: RewardedAd) {
                loadingRewarded = false
                rewardedRetry = 0
                rewarded = ad
                sendState()
            }

            override fun onAdFailedToLoad(error: LoadAdError) {
                loadingRewarded = false
                Log.w(TAG, "Rewarded ad didn't load: ${error.message}")
                handler.postDelayed({ loadRewarded() }, retryDelay(rewardedRetry++))
            }
        })
    }

    private fun showRewarded() {
        val ad = rewarded ?: return reportError("show_rewarded", "not loaded")
        rewarded = null
        var earned = false
        ad.fullScreenContentCallback = object : FullScreenContentCallback() {
            override fun onAdDismissedFullScreenContent() {
                WlwBridge.post(name, "rewarded_done", JSONObject().put("rewarded", earned))
                loadRewarded()
                sendState()
            }

            override fun onAdFailedToShowFullScreenContent(adError: AdError) {
                reportError("show_rewarded", adError.message)
                loadRewarded()
            }
        }
        ad.show(activity) { earned = true }  // the player watched enough to earn the reward
    }

    // ---- Interstitial ----

    private fun loadInterstitial() {
        if (destroyed || loadingInterstitial || interstitial != null || interstitialUnit.isEmpty()) return
        loadingInterstitial = true
        InterstitialAd.load(activity, interstitialUnit, AdRequest.Builder().build(), object : InterstitialAdLoadCallback() {
            override fun onAdLoaded(ad: InterstitialAd) {
                loadingInterstitial = false
                interstitialRetry = 0
                interstitial = ad
                sendState()
            }

            override fun onAdFailedToLoad(error: LoadAdError) {
                loadingInterstitial = false
                Log.w(TAG, "Interstitial didn't load: ${error.message}")
                handler.postDelayed({ loadInterstitial() }, retryDelay(interstitialRetry++))
            }
        })
    }

    private fun showInterstitial() {
        val ad = interstitial ?: return reportError("show_interstitial", "not loaded")
        interstitial = null
        ad.fullScreenContentCallback = object : FullScreenContentCallback() {
            override fun onAdDismissedFullScreenContent() {
                WlwBridge.post(name, "interstitial_done")
                loadInterstitial()
                sendState()
            }

            override fun onAdFailedToShowFullScreenContent(adError: AdError) {
                reportError("show_interstitial", adError.message)
                loadInterstitial()
            }
        }
        ad.show(activity)
    }

    // ---- To C++ ----

    private fun sendState() {
        val privacy = consent?.privacyOptionsRequirementStatus ==
            ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED
        WlwBridge.post(name, "state", JSONObject()
            .put("rewarded_ready", rewarded != null)
            .put("interstitial_ready", interstitial != null)
            .put("privacy_required", privacy))
    }

    private fun reportError(method: String, message: String) {
        Log.w(TAG, "Ads: $method: $message")
        WlwBridge.post(name, "error", JSONObject().put("method", method).put("message", message))
    }

    private companion object {
        const val TAG = "WLW"
        // 5 s, 10 s, 20 s... up to 5 minutes: no hammering the network when there's no fill.
        fun retryDelay(failures: Int): Long = minOf(5_000L shl minOf(failures, 6), 300_000L)
    }
}
