package com.wlw

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.AudioPlaybackConfiguration
import android.os.Build
import android.os.Handler
import android.os.Looper
import org.json.JSONObject

// Watches what else plays on the device while the game is on screen, and tells the game (bridge
// service "audio", handled by Application):
//   interrupted / uninterrupted                 a call (ringing or talking), an alarm, the voice
//                                               assistant: the game goes silent and pauses
//   other_music_started / other_music_stopped   the player's own music (Spotify, a podcast...):
//                                               the game's music steps aside, its sound effects mix in
//
// It doesn't take Android's audio focus on purpose: taking it would stop the player's music
// whenever the game opens. It only looks (playback callback + audio mode: no permission needed),
// like an iOS game in the "ambient" audio category. Android 8.0+ (API 26); older phones get
// only the background pause.
internal class AudioWatcher(context: Context) : WlwService {
    override val name = "audio"
    private val manager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private val handler = Handler(Looper.getMainLooper())
    private var interrupted = false   // as last told to the game
    private var otherMusic = false

    private val playback: Any? =  // an AudioPlaybackCallback (API 26), kept as Any for older phones
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            object : AudioManager.AudioPlaybackCallback() {
                override fun onPlaybackConfigChanged(configs: MutableList<AudioPlaybackConfiguration>) = update(configs)
            }
        } else {
            null
        }

    private val modeListener: Any? =  // the audio mode changes (a call starts / ends), API 31
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            AudioManager.OnModeChangedListener { update(null) }
        } else {
            null
        }

    // On screen: watch, and report how things stand now (they may have changed while we were away).
    fun start() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        manager.registerAudioPlaybackCallback(playback as AudioManager.AudioPlaybackCallback, handler)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            manager.addOnModeChangedListener({ handler.post(it) }, modeListener as AudioManager.OnModeChangedListener)
        }
        update(null)
    }

    // From C++ once its handler is set ("refresh"): reports going out before the native code
    // started were dropped, so say how things stand again.
    override fun onCall(method: String, args: JSONObject) {
        if (method != "refresh") return
        if (interrupted) WlwBridge.post("audio", "interrupted")
        if (otherMusic) WlwBridge.post("audio", "other_music_started")
        update(null)
    }

    fun stop() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        handler.removeCallbacks(settle)
        manager.unregisterAudioPlaybackCallback(playback as AudioManager.AudioPlaybackCallback)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            manager.removeOnModeChangedListener(modeListener as AudioManager.OnModeChangedListener)
        }
    }

    // configs: the active players (the callback's list), or null to ask for them. `settled`: the
    // other music has been playing for SETTLE_MS.
    private fun update(configs: List<AudioPlaybackConfiguration>?, settled: Boolean = false) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val usages = (configs ?: manager.activePlaybackConfigurations).map { it.audioAttributes.usage }
        // Any mode but normal is telephony: ringing, in a call, a VoIP call, call screening.
        val inCall = manager.mode != AudioManager.MODE_NORMAL && manager.mode >= 0
        var media = usages.any { it == AudioAttributes.USAGE_MEDIA }
        // Music has to keep playing a moment before ours steps aside: the ad SDK in our own process
        // briefly opens a media player when it loads an ad (players don't say whose they are).
        if (media && !otherMusic && !settled) {
            handler.removeCallbacks(settle)
            handler.postDelayed(settle, SETTLE_MS)
            media = false
        }
        set(inCall || usages.any { it in INTERRUPTING }, otherMusic = media)
    }

    private val settle = Runnable { update(null, settled = true) }

    private fun set(interrupted: Boolean, otherMusic: Boolean) {
        if (interrupted != this.interrupted) {
            this.interrupted = interrupted
            WlwBridge.post("audio", if (interrupted) "interrupted" else "uninterrupted")
        }
        if (otherMusic != this.otherMusic) {
            this.otherMusic = otherMusic
            WlwBridge.post("audio", if (otherMusic) "other_music_started" else "other_music_stopped")
        }
    }

    private companion object {
        const val SETTLE_MS = 1000L
        val INTERRUPTING = setOf(
            AudioAttributes.USAGE_NOTIFICATION_RINGTONE,
            AudioAttributes.USAGE_VOICE_COMMUNICATION,
            AudioAttributes.USAGE_ALARM,
            AudioAttributes.USAGE_ASSISTANT,
        )
    }
}
