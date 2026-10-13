package com.wlw

import android.app.NativeActivity
import android.os.Bundle

// NativeActivity (which runs the game's C++ android_main) plus the native bridge's services.
// A game subclasses it to say which SDK services it uses:
//
//   class MyGameActivity : WlwActivity() {
//       override fun createServices() = listOf(AdMobService(this))
//   }
//
// and names it in its AndroidManifest.xml instead of android.app.NativeActivity.
open class WlwActivity : NativeActivity() {
    open fun createServices(): List<WlwService> = emptyList()

    private lateinit var audioWatcher: AudioWatcher  // the engine's own service: calls, the player's music

    override fun onCreate(savedInstanceState: Bundle?) {
        // Before super.onCreate: that starts the native thread, which may call a service at once.
        audioWatcher = AudioWatcher(this)
        WlwBridge.attach(this, createServices() + audioWatcher)
        super.onCreate(savedInstanceState)
    }

    // Other apps' sound only matters while we're on screen.
    override fun onResume() {
        super.onResume()
        audioWatcher.start()
    }

    override fun onPause() {
        audioWatcher.stop()
        super.onPause()
    }

    override fun onDestroy() {
        WlwBridge.detach()
        super.onDestroy()
    }
}
