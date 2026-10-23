#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wlw/core/connection.h"

// Logging, like Unreal's UE_LOG: a CATEGORY (which system speaks) and a LEVEL (how important).
//
//   WLW_LOG(LogRender, Warning, "texture %s is %dx%d", name, w, h);   // printf-style, checked by the compiler
//   WLW_LOG_INFO("hello");                                          // category General
//
//   WLW_LOG_CATEGORY(LogBluzzle);   // a game's own category, at namespace scope in a header (one object
//                                   // everywhere; shown as "Bluzzle", the "Log" prefix is dropped)
//
// Levels below WLW_LOG_COMPILE_LEVEL compile to nothing (their arguments aren't evaluated): Debug and up
// in debug builds, Info and up in release. At runtime each category has its own level (Info by default),
// set with SetLogFilter("Render=Verbose,Audio=Warning,*=Info") (the WLW_LOG environment variable on
// desktop, the console later).
//
// Every message becomes a LogRecord that goes to the sinks: the platform's output (console / logcat), the
// in-memory history (RecentLogs: the last kLogHistorySize records, for the console and crash reports), a
// log file if one was started (StartLogFile; the Application does it in debug builds), and any added with
// AddLogSink. Thread-safe: any thread may log; sinks are called one record at a time.
//
// Fatal: the record is written and flushed everywhere, then the program stops (a debugger, if attached,
// breaks first). Assertions are in assert.h.
namespace wlw {

enum class LogLevel : uint8_t { Verbose, Debug, Info, Warning, Error, Fatal, Off };

const char* LogLevelName(LogLevel level);  // "verbose" ... "fatal", "off"
// "verbose", "Warning", "warn", "error", "off"... (case-insensitive); nullopt if unknown.
std::optional<LogLevel> ParseLogLevel(std::string_view text);

// A category: a name and its runtime level. Made by WLW_LOG_CATEGORY; registered on construction so
// filters can find it by name (also filters set before it existed).
class LogCategory {
 public:
  explicit LogCategory(const char* name);
  LogCategory(const LogCategory&) = delete;
  LogCategory& operator=(const LogCategory&) = delete;

  const char* Name() const { return name_; }
  LogLevel Level() const { return static_cast<LogLevel>(level_.load(std::memory_order_relaxed)); }
  void SetLevel(LogLevel level) { level_.store(static_cast<uint8_t>(level), std::memory_order_relaxed); }
  bool Enabled(LogLevel level) const { return level >= Level() && level != LogLevel::Off; }

 private:
  const char* name_;
  std::atomic<uint8_t> level_;
};

struct LogRecord {
  double seconds = 0.0;  // since the program started
  int thread = 0;        // small id per thread, in the order threads first log (see LogThreadId)
  LogLevel level = LogLevel::Info;
  const char* category = "";  // the category's name
  std::string message;
  const char* file = "";  // where it was logged
  int line = 0;
};

// "[12.345][T0][Render][warning] message" (the level is left out for Info).
std::string FormatLogRecord(const LogRecord& record);

// ---- Filters ----
// "Render=Verbose,Audio=Warning,*=Info": per category (by name, without "Log"); "*" or a bare level
// ("Debug") sets every category, applied first. Names not known yet apply when they register. False if
// anything didn't parse (the parts that did still apply).
bool SetLogFilter(std::string_view filter);
// Every category (and the default for ones registered later).
void SetLogLevel(LogLevel level);
// Registered categories' names (for the console, tests).
std::vector<std::string> LogCategoryNames();

// ---- Sinks ----
using LogSink = std::function<void(const LogRecord&)>;
// Called for every record that passes the filters, on the thread that logged, never two at once. A
// sink must not log (that record would be dropped). Disconnect the returned Connection (from any
// thread, but not from inside a sink) to remove it; once Disconnect returns, the sink isn't called again.
core::Connection AddLogSink(LogSink sink);
// The platform's output (stdout/stderr, logcat): on unless turned off (e.g. a quiet test).
void SetPlatformLogOutput(bool on);

// The last kLogHistorySize records (oldest first): always kept, cheap.
inline constexpr size_t kLogHistorySize = 512;
std::vector<LogRecord> RecentLogs();
void ClearRecentLogs();

// Writes every record to `path` too (one line each, flushed as written so a crash loses nothing). An existing file is
// kept as <path>.prev first (the run before: what a tester sends after a crash). Replaces any open log
// file. False if it can't be opened.
bool StartLogFile(const std::string& path);
void StopLogFile();

// This thread's id in records (0 = the first thread that asked: the Application asks on the main
// thread first).
int LogThreadId();

namespace detail {
void LogWrite(const LogCategory& category, LogLevel level, const char* file, int line, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;
}  // namespace detail

// Old entry point (category General); prefer the macros.
void Log(LogLevel level, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace wlw

// ---- Declaring categories ----
#define WLW_LOG_CATEGORY(Name) \
  inline ::wlw::LogCategory Name { #Name }

// ---- Compile-time level: 0 Verbose, 1 Debug, 2 Info, 3 Warning, 4 Error, 5 Fatal ----
#ifndef WLW_LOG_COMPILE_LEVEL
#ifdef NDEBUG
#define WLW_LOG_COMPILE_LEVEL 2
#else
#define WLW_LOG_COMPILE_LEVEL 1
#endif
#endif

#define WLW_LOG(Category, Level, ...)                                                                 \
  do {                                                                                                \
    if constexpr (static_cast<int>(::wlw::LogLevel::Level) >= WLW_LOG_COMPILE_LEVEL) {                \
      if ((Category).Enabled(::wlw::LogLevel::Level)) {                                               \
        ::wlw::detail::LogWrite((Category), ::wlw::LogLevel::Level, __FILE__, __LINE__, __VA_ARGS__); \
      }                                                                                               \
    }                                                                                                 \
  } while (0)

namespace wlw {
// The engine's categories. Games make their own with WLW_LOG_CATEGORY.
WLW_LOG_CATEGORY(LogGeneral);  // WLW_LOG_INFO / WARN / ERROR, Log()
WLW_LOG_CATEGORY(LogCore);     // asserts, files, the basics
WLW_LOG_CATEGORY(LogApp);      // Application, systems, screens, quality levels
WLW_LOG_CATEGORY(LogRender);   // the renderer, GPU resources, viewports, post passes
WLW_LOG_CATEGORY(LogShader);   // shader building and compiling
WLW_LOG_CATEGORY(LogAudio);
WLW_LOG_CATEGORY(LogPlatform);  // OS windows, the native bridge, save files
WLW_LOG_CATEGORY(LogUI);        // canvas, fonts, widgets
WLW_LOG_CATEGORY(LogFX);        // particles, flow particles
}  // namespace wlw

#define WLW_LOG_VERBOSE(...) WLW_LOG(::wlw::LogGeneral, Verbose, __VA_ARGS__)
#define WLW_LOG_DEBUG(...) WLW_LOG(::wlw::LogGeneral, Debug, __VA_ARGS__)
#define WLW_LOG_INFO(...) WLW_LOG(::wlw::LogGeneral, Info, __VA_ARGS__)
#define WLW_LOG_WARN(...) WLW_LOG(::wlw::LogGeneral, Warning, __VA_ARGS__)
#define WLW_LOG_ERROR(...) WLW_LOG(::wlw::LogGeneral, Error, __VA_ARGS__)
