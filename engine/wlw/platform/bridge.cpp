#include "wlw/platform/bridge.h"

#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "wlw/core/log.h"

namespace wlw::platform {
namespace {

struct Message {
  std::string service;
  std::string event;
  std::string json;
};

// The queue is written by platform threads (Android's UI thread, SDK threads) and read by the
// game thread: a mutex keeps it consistent. Messages are rare, so a lock is plenty.
std::mutex g_mutex;
std::vector<Message> g_queue;

// Handlers are only touched on the game thread.
std::map<std::string, BridgeHandler>& Handlers() {
  static std::map<std::string, BridgeHandler> handlers;
  return handlers;
}

}  // namespace

void PostBridgeMessage(std::string service, std::string event, std::string json) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_queue.push_back({std::move(service), std::move(event), std::move(json)});
}

void SetBridgeHandler(const std::string& service, BridgeHandler handler) {
  if (handler) {
    Handlers()[service] = std::move(handler);
  } else {
    Handlers().erase(service);
  }
}

int DispatchBridgeMessages() {
  std::vector<Message> messages;
  {
    // Take the whole queue under the lock, run the handlers without it: a handler may post or
    // call into the bridge again without deadlocking.
    std::lock_guard<std::mutex> lock(g_mutex);
    messages.swap(g_queue);
  }
  for (const Message& message : messages) {
    const auto it = Handlers().find(message.service);
    if (it == Handlers().end()) {
      WLW_LOG_WARN("Bridge: no handler for %s.%s", message.service.c_str(), message.event.c_str());
      continue;
    }
    const BridgeHandler handler = it->second;  // a copy: the handler may replace itself
    handler(message.event, message.json);
  }
  return static_cast<int>(messages.size());
}

}  // namespace wlw::platform
