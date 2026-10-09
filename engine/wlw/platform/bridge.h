#pragma once

#include <functional>
#include <string>

// The native bridge: messages between the game (C++) and code written in the platform's own
// language - Kotlin on Android (Swift on iOS later). Platform SDKs (ads, sign-in, cloud saves,
// purchases, reviews...) only exist in that language, so a game reaches them through here.
//
// Everything crossing the bridge is a "service" name, a method/event name and JSON text: one
// small, fixed set of JNI functions serves every SDK, instead of hand-written JNI per feature.
// (JSON costs a few microseconds per message; these are rare, user-paced events.)
//
//   C++ -> platform:  BridgeCall("ads", "show_rewarded", "{}")
//                     Android: com.wlw.WlwBridge.call(...) runs the service's onCall on the UI thread
//   platform -> C++:  WlwBridge.post("ads", "rewarded_done", "{\"rewarded\":true}") from any thread
//                     -> queued -> the handler set with SetBridgeHandler("ads", ...) runs on the
//                        GAME thread during the next frame (Application calls
//                        DispatchBridgeMessages), so handlers can touch game state freely.
//
// The queue is shared code (bridge.cpp); only BridgeCall/HasBridge differ per platform.
namespace wlw::platform {

// Whether there's a platform side to talk to (true on Android; false on desktop, where games use
// fakes instead).
bool HasBridge();

// Sends a call to the platform side. False if there's no bridge or the call couldn't be made.
bool BridgeCall(const std::string& service, const std::string& method, const std::string& json_args = "{}");

// Queues a message for the game thread. Thread-safe. The platform side calls it; C++ fakes and
// tests may too.
void PostBridgeMessage(std::string service, std::string event, std::string json);

// Who handles messages for `service` (one handler per service; nullptr removes it). Messages for
// a service without a handler are dropped (with a warning).
using BridgeHandler = std::function<void(const std::string& event, const std::string& json)>;
void SetBridgeHandler(const std::string& service, BridgeHandler handler);

// Runs the handlers for every queued message, in order. Game thread only (Application calls it
// once per frame). Returns how many messages were delivered.
int DispatchBridgeMessages();

}  // namespace wlw::platform
