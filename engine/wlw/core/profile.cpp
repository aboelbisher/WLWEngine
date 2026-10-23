#include "wlw/core/profile.h"

#if WLW_PROFILE

#include "wlw/core/log.h"

#if defined(WLW_PLATFORM_ANDROID)
#include <android/trace.h>
#include <dlfcn.h>
#endif

namespace wlw::profile {

namespace {

core::Connection log_sink;

// Tracy's message colours (0xRRGGBB; 0 = the default) by level.
uint32_t MessageColor(LogLevel level) {
  switch (level) {
    case LogLevel::Warning: return 0xE0C040;
    case LogLevel::Error:
    case LogLevel::Fatal: return 0xF05050;
    case LogLevel::Verbose:
    case LogLevel::Debug: return 0x909090;
    default: return 0;
  }
}

#if defined(WLW_PLATFORM_ANDROID)
// ATrace_setCounter is API 29; the engine runs from API 24.
using SetCounterFn = void (*)(const char* name, int64_t value);
SetCounterFn LoadSetCounter() {
  void* android = dlopen("libandroid.so", RTLD_NOW);
  return android ? reinterpret_cast<SetCounterFn>(dlsym(android, "ATrace_setCounter")) : nullptr;
}
#endif

}  // namespace

void SetThreadName(const char* name) {
  tracy::SetThreadName(name);  // also the OS's name for the thread where it has one (Windows, Linux, Android)
}

bool Connected() { return TracyIsConnected; }

void Start() {
  SetThreadName("Game");
  if (log_sink.Connected()) return;
  // Only while a viewer is connected (Tracy drops messages otherwise): the log lines on the timeline.
  log_sink = AddLogSink([](const LogRecord& record) {
    if (!TracyIsConnected) return;
    const std::string text = std::string(record.category) + ": " + record.message;
    TracyMessageC(text.data(), text.size(), MessageColor(record.level));
  });
}

void Stop() {
  log_sink.Disconnect();  // here, not at static destruction (the log may be gone by then)
}

#if defined(WLW_PLATFORM_ANDROID)
void BeginSystemSection(const char* name) { ATrace_beginSection(name); }
void EndSystemSection() { ATrace_endSection(); }
void SystemCounter(const char* name, int64_t value) {
  static const SetCounterFn set_counter = LoadSetCounter();
  if (set_counter && ATrace_isEnabled()) set_counter(name, value);
}
#endif

}  // namespace wlw::profile

#else

namespace wlw::profile {
void SetThreadName(const char*) {}
bool Connected() { return false; }
void Start() {}
void Stop() {}
}  // namespace wlw::profile

#endif
