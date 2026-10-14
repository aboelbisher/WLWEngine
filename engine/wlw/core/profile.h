#pragma once

#include <cstdint>

// Profiling: zones, frame marks, plots and messages for Tracy (a viewer on a PC shows every frame on every
// thread), plus ATrace sections and counters on Android (Perfetto / Android Studio show them next to CPU
// frequency, cores, thermal and vsync). Like Unreal's TRACE_CPUPROFILER_EVENT_SCOPE or Unity's ProfilerMarker.
//
//   WLW_PROFILE_SCOPE("Gather");             a zone from here to the end of the scope (a string literal)
//   WLW_PROFILE_FUNCTION();                  a zone named after the function
//   WLW_PROFILE_SCOPE_DYNAMIC(name);         a zone named at run time (a null-terminated const char*: a
//                                            system's or a post pass's name; costs a little more)
//   WLW_PROFILE_TEXT(text, size);            text on the current zone (which file, how many...)
//   WLW_PROFILE_FRAME();                     the end of a frame (the Application marks it after present)
//   WLW_PROFILE_PLOT("Draw calls", value);   a value graphed over time (a string literal); whole numbers, so
//                                            units go in the name ("Frame us")
//   WLW_PROFILE_THREAD_NAME("Audio decode"); names the calling thread
//   WLW_PROFILE_MESSAGE(text);               a line on the timeline (every log record becomes one)
//
// One zone per scope: a second one needs its own block. Built in only with the CMake option WLW_PROFILE (the
// *-profile presets: optimized, with symbols); otherwise every macro is empty and its arguments aren't
// evaluated, so zones stay in shipping code at no cost. With it, zones cost a branch until a viewer connects
// (TRACY_ON_DEMAND). Any thread may use them. How to connect: CLAUDE.md, "Profiling".
namespace wlw::profile {

#if WLW_PROFILE
inline constexpr bool kEnabled = true;
#else
inline constexpr bool kEnabled = false;
#endif

// Profile builds: the calling thread's name in Tracy (and the OS, where it has thread names). Else nothing.
void SetThreadName(const char* name);
// A Tracy viewer is connected (always false without WLW_PROFILE).
bool Connected();
// Names the calling thread "Game" and sends log records to Tracy as messages. The Application calls it.
void Start();
void Stop();

}  // namespace wlw::profile

#define WLW_PROFILE_CONCAT_INNER_(a, b) a##b
#define WLW_PROFILE_CONCAT_(a, b) WLW_PROFILE_CONCAT_INNER_(a, b)

#if WLW_PROFILE

#include <cstring>

#include <tracy/Tracy.hpp>

#if defined(WLW_PLATFORM_ANDROID)
namespace wlw::profile {
// ATrace (android/trace.h): sections nest per thread; counters need API 29 (looked up at run time).
void BeginSystemSection(const char* name);
void EndSystemSection();
void SystemCounter(const char* name, int64_t value);

class SystemSection {
 public:
  explicit SystemSection(const char* name) { BeginSystemSection(name); }
  ~SystemSection() { EndSystemSection(); }
  SystemSection(const SystemSection&) = delete;
  SystemSection& operator=(const SystemSection&) = delete;
};
}  // namespace wlw::profile
#define WLW_PROFILE_SYSTEM_SECTION_(name) \
  ::wlw::profile::SystemSection WLW_PROFILE_CONCAT_(wlw_profile_section_, __LINE__)(name)
#define WLW_PROFILE_SYSTEM_COUNTER_(name, value) ::wlw::profile::SystemCounter(name, value)
#else
#define WLW_PROFILE_SYSTEM_SECTION_(name) static_cast<void>(0)
#define WLW_PROFILE_SYSTEM_COUNTER_(name, value) static_cast<void>(0)
#endif

#define WLW_PROFILE_SCOPE(name) \
  ZoneScopedN(name);            \
  WLW_PROFILE_SYSTEM_SECTION_(name)
#define WLW_PROFILE_FUNCTION() \
  ZoneScoped;                  \
  WLW_PROFILE_SYSTEM_SECTION_(__func__)
// A transient zone: its own entry per name in Tracy's statistics (the name is copied while a viewer is connected).
#define WLW_PROFILE_SCOPE_DYNAMIC(name)                                                              \
  const char* const WLW_PROFILE_CONCAT_(wlw_profile_name_, __LINE__) = (name);                       \
  SuppressVarShadowWarning(                                                                          \
      ZoneTransientN(___tracy_scoped_zone, WLW_PROFILE_CONCAT_(wlw_profile_name_, __LINE__), true)); \
  WLW_PROFILE_SYSTEM_SECTION_(WLW_PROFILE_CONCAT_(wlw_profile_name_, __LINE__))
#define WLW_PROFILE_TEXT(text, size) ZoneText(text, size)
#define WLW_PROFILE_FRAME() FrameMark
#define WLW_PROFILE_PLOT(name, value)                               \
  do {                                                              \
    const int64_t wlw_profile_value_ = static_cast<int64_t>(value); \
    TracyPlot(name, wlw_profile_value_);                            \
    WLW_PROFILE_SYSTEM_COUNTER_(name, wlw_profile_value_);          \
  } while (0)
#define WLW_PROFILE_THREAD_NAME(name) ::wlw::profile::SetThreadName(name)
#define WLW_PROFILE_MESSAGE(text)                                    \
  do {                                                               \
    const char* const wlw_profile_text_ = (text);                    \
    TracyMessage(wlw_profile_text_, std::strlen(wlw_profile_text_)); \
  } while (0)

#else

#define WLW_PROFILE_SCOPE(name) static_cast<void>(0)
#define WLW_PROFILE_FUNCTION() static_cast<void>(0)
#define WLW_PROFILE_SCOPE_DYNAMIC(name) static_cast<void>(0)
#define WLW_PROFILE_TEXT(text, size) static_cast<void>(0)
#define WLW_PROFILE_FRAME() static_cast<void>(0)
#define WLW_PROFILE_PLOT(name, value) static_cast<void>(0)
#define WLW_PROFILE_THREAD_NAME(name) static_cast<void>(0)
#define WLW_PROFILE_MESSAGE(text) static_cast<void>(0)

#endif
