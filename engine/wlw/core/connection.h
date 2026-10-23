#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace wlw::core {

// What a callback source keeps per registered callback (a log sink, a cvar's OnChange, a window
// overlay). A Connection points at it weakly, so the source may die first.
class ConnectionSlot {
 public:
  virtual ~ConnectionSlot() = default;
  virtual void Disconnect() = 0;
  virtual bool Connected() const = 0;
};

// The handle a registration returns (like boost.signals2's connection, entt's connection): a plain
// copyable value. Disconnect() unhooks the callback; it's safe after the source is gone (then it does
// nothing) and safe to call twice. Dropping a Connection does NOT unhook anything: keep it only if
// the callback must be removed before its source dies, or hold a ScopedConnection to unhook on scope exit.
class Connection {
 public:
  Connection() = default;
  explicit Connection(std::weak_ptr<ConnectionSlot> slot) : slot_(std::move(slot)) {}

  void Disconnect() {
    if (auto slot = slot_.lock()) slot->Disconnect();
    slot_.reset();
  }
  // Still hooked up: not disconnected, and its source is alive.
  bool Connected() const {
    const auto slot = slot_.lock();
    return slot && slot->Connected();
  }

 private:
  std::weak_ptr<ConnectionSlot> slot_;
};

// Owns a Connection and disconnects it when destroyed or reassigned (opt-in RAII). Move-only.
class ScopedConnection {
 public:
  ScopedConnection() = default;
  ScopedConnection(Connection connection) : connection_(std::move(connection)) {}  // NOLINT: implicit on purpose
  ~ScopedConnection() { connection_.Disconnect(); }
  ScopedConnection(ScopedConnection&& other) noexcept : connection_(std::exchange(other.connection_, {})) {}
  ScopedConnection& operator=(ScopedConnection&& other) noexcept {
    if (this != &other) {
      connection_.Disconnect();
      connection_ = std::exchange(other.connection_, {});
    }
    return *this;
  }
  ScopedConnection(const ScopedConnection&) = delete;
  ScopedConnection& operator=(const ScopedConnection&) = delete;

  void Disconnect() { connection_.Disconnect(); }
  bool Connected() const { return connection_.Connected(); }
  // Gives the connection back without disconnecting it.
  Connection Release() { return std::exchange(connection_, {}); }

 private:
  Connection connection_;
};

// A list of callbacks for one thread (no locking): Add returns a Connection, Call runs them in the
// order added. A callback may disconnect itself or others, or add new ones (called from the next
// Call on), while it runs.
template <typename... Args>
class CallbackList {
 public:
  using Callback = std::function<void(Args...)>;

  CallbackList() = default;
  CallbackList(const CallbackList&) = delete;
  CallbackList& operator=(const CallbackList&) = delete;

  Connection Add(Callback callback) {
    DropDisconnected();
    auto entry = std::make_shared<Entry>(std::move(callback));
    entries_.push_back(entry);
    return Connection(entry);
  }

  void Call(Args... args) {
    ++calling_;
    const size_t count = entries_.size();  // added during the call: from the next one on
    for (size_t i = 0; i < count; ++i) {
      // A copy of the pointer: the callback may add entries (reallocating the vector).
      const std::shared_ptr<Entry> entry = entries_[i];
      if (entry->connected) entry->callback(args...);
    }
    --calling_;
    DropDisconnected();
  }

  // Connected callbacks.
  size_t Size() const {
    size_t count = 0;
    for (const auto& entry : entries_) count += entry->connected ? 1 : 0;
    return count;
  }
  bool Empty() const { return Size() == 0; }

 private:
  struct Entry final : ConnectionSlot {
    explicit Entry(Callback fn) : callback(std::move(fn)) {}
    void Disconnect() override { connected = false; }  // erased later: it may be running now
    bool Connected() const override { return connected; }
    Callback callback;
    bool connected = true;
  };

  void DropDisconnected() {
    if (calling_ == 0) std::erase_if(entries_, [](const std::shared_ptr<Entry>& entry) { return !entry->connected; });
  }

  std::vector<std::shared_ptr<Entry>> entries_;
  int calling_ = 0;
};

}  // namespace wlw::core
