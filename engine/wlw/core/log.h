#pragma once

namespace wlw {

enum class LogLevel { Info, Warning, Error };

// printf-style logging that works everywhere: stdout/stderr on desktop, logcat on Android
// (filter with: adb logcat -s WLW).
void Log(LogLevel level, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace wlw

#define WLW_LOG_INFO(...) ::wlw::Log(::wlw::LogLevel::Info, __VA_ARGS__)
#define WLW_LOG_WARN(...) ::wlw::Log(::wlw::LogLevel::Warning, __VA_ARGS__)
#define WLW_LOG_ERROR(...) ::wlw::Log(::wlw::LogLevel::Error, __VA_ARGS__)
