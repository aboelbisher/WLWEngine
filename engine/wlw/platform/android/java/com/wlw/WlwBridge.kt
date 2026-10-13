package com.wlw

import android.app.Activity
import android.util.Log
import org.json.JSONObject

// The Kotlin end of the native bridge (engine/wlw/platform/bridge.h).
//
// C++ calls call(service, method, json) on the game thread; it's handed to the service on the UI
// thread, where Android SDKs expect to be used. Services answer with post(service, event, json)
// from any thread; the message is queued in C++ and handled on the game thread.
object WlwBridge {
    private const val TAG = "WLW"
    private val services = mutableMapOf<String, WlwService>()
    private var activity: Activity? = null

    // Called by WlwActivity before the native code starts.
    fun attach(activity: Activity, services: List<WlwService>) {
        this.activity = activity
        this.services.clear()
        for (service in services) this.services[service.name] = service
    }

    fun detach() {
        for (service in services.values) service.onDestroy()
        services.clear()
        activity = null
    }

    // From C++ (game thread).
    @JvmStatic
    fun call(service: String, method: String, json: String) {
        val target = services[service]
        val host = activity
        if (target == null || host == null) {
            Log.w(TAG, "Bridge: no service '$service' for $method")
            post(service, "error", JSONObject().put("method", method).put("message", "no such service"))
            return
        }
        val args = try { JSONObject(json) } catch (e: Exception) { JSONObject() }
        host.runOnUiThread {
            try {
                target.onCall(method, args)
            } catch (e: Exception) {
                Log.e(TAG, "Bridge: $service.$method failed", e)
                post(service, "error", JSONObject().put("method", method).put("message", e.toString()))
            }
        }
    }

    // To C++ (any thread).
    fun post(service: String, event: String, json: JSONObject = JSONObject()) {
        try {
            nativePost(service, event, json.toString())
        } catch (e: UnsatisfiedLinkError) {  // the native code hasn't started (registered it) yet
            Log.w(TAG, "Bridge: native side not ready, dropped $service.$event")
        }
    }

    // Registered by the engine (android_platform.cpp, RegisterNatives) when the native code starts.
    @JvmStatic
    private external fun nativePost(service: String, event: String, json: String)
}

// One SDK wrapped for the game: ads, sign-in, cloud saves... `name` is what C++ calls it by.
interface WlwService {
    val name: String
    fun onCall(method: String, args: JSONObject)  // on the UI thread
    fun onDestroy() {}
}
