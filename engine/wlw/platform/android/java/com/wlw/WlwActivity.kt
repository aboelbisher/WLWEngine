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

    override fun onCreate(savedInstanceState: Bundle?) {
        // Before super.onCreate: that starts the native thread, which may call a service at once.
        WlwBridge.attach(this, createServices())
        super.onCreate(savedInstanceState)
    }

    override fun onDestroy() {
        WlwBridge.detach()
        super.onDestroy()
    }
}
