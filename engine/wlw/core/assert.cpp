#include "wlw/core/assert.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "wlw/platform/platform.h"

namespace wlw {
namespace {

std::mutex g_handler_mutex;
AssertHandler g_handler;

std::string Format(const char* format, va_list args) {
  char buffer[1024];
  std::vsnprintf(buffer, sizeof(buffer), format + 1, args);  // + 1: WLW_DETAIL_FORMAT's space
  return buffer;
}

void Fail(AssertFailure failure) {
  AssertHandler handler;
  {
    std::lock_guard lock(g_handler_mutex);
    handler = g_handler;
  }
  if (handler) {
    handler(failure);
    return;
  }
  // The Fatal log writes and flushes every sink, breaks into a debugger if there is one, and aborts.
  WLW_LOG(LogCore, Fatal, "%s(%s) failed%s%s\n  At: %s:%d", failure.kind, failure.expression,
          failure.message.empty() ? "" : ": ", failure.message.c_str(), failure.file, failure.line);
  std::abort();  // only if Fatal was filtered out
}

}  // namespace

AssertHandler SetAssertHandler(AssertHandler handler) {
  std::lock_guard lock(g_handler_mutex);
  std::swap(g_handler, handler);
  return handler;
}

namespace detail {

void AssertFailed(const char* expression, const char* file, int line, const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::string message = Format(format, args);
  va_end(args);
  Fail({"WLW_ASSERT", expression, file, line, std::move(message)});
}

bool VerifyFailed(const char* expression, const char* file, int line, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = Format(format, args);
  va_end(args);
  if (LogCore.Enabled(LogLevel::Error)) {
    detail::LogWrite(LogCore, LogLevel::Error, file, line, "%s%sWLW_VERIFY(%s) failed\n  At: %s:%d", message.c_str(),
                     message.empty() ? "" : "\n  ", expression, file, line);
  }
#ifndef NDEBUG
  if (platform::IsDebuggerAttached()) platform::BreakIntoDebugger();
#endif
  return false;
}

void Unreachable(const char* file, int line, const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::string message = Format(format, args);
  va_end(args);
  Fail({"WLW_UNREACHABLE", "", file, line, std::move(message)});
  std::abort();  // a handler that returned
}

}  // namespace detail
}  // namespace wlw
