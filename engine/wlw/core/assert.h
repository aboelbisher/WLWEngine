#pragma once

#include <functional>
#include <string>

#include "wlw/core/log.h"

// Assertions, like Unreal's check / verify / ensure:
//
//   WLW_ASSERT(index < size, "index %d of %d", index, size);
//       A programmer error: this can't happen if the code is right. Debug builds stop (Fatal log, the
//       debugger breaks, abort); release builds compile it to nothing (the condition isn't evaluated)
//       unless WLW_ENABLE_ASSERTS is on (CMake option).
//
//   if (!WLW_VERIFY(file, "can't read %s", path)) return false;
//       Something that can go wrong at runtime and is handled: the condition is always evaluated; when
//       it's false an Error is logged with the condition and file:line (a debugger, if attached, breaks
//       in debug builds) and it returns false, so the caller recovers.
//
//   WLW_UNREACHABLE("unknown blend mode %d", mode);
//       Code that must never run (every build): Fatal log and stop.
//
// The message is optional and printf-style; its format must be a string literal. Tests replace the
// stopping with SetAssertHandler (e.g. a handler that throws).
namespace wlw {

struct AssertFailure {
  const char* kind;        // "WLW_ASSERT" or "WLW_UNREACHABLE"
  const char* expression;  // the condition as written ("" for WLW_UNREACHABLE)
  const char* file;
  int line;
  std::string message;
};

// Called instead of the Fatal log + abort when an assert or WLW_UNREACHABLE fails. It may throw (tests);
// if it returns from WLW_UNREACHABLE the program still aborts. Null restores the default. Returns the
// previous handler. Not for WLW_VERIFY, which only logs.
using AssertHandler = std::function<void(const AssertFailure&)>;
AssertHandler SetAssertHandler(AssertHandler handler);

namespace detail {
void AssertFailed(const char* expression, const char* file, int line, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
bool VerifyFailed(const char* expression, const char* file, int line, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
[[noreturn]] void Unreachable(const char* file, int line, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
}  // namespace detail

}  // namespace wlw

#ifndef WLW_ASSERTS_ENABLED
#if defined(WLW_ENABLE_ASSERTS) || !defined(NDEBUG)
#define WLW_ASSERTS_ENABLED 1
#else
#define WLW_ASSERTS_ENABLED 0
#endif
#endif

// The optional message: a space in front of the format (skipped when formatting), so no message is " "
// rather than an empty format, which compilers warn about; the format must be a literal.
#define WLW_DETAIL_FORMAT(...) " " __VA_ARGS__

#if WLW_ASSERTS_ENABLED
#define WLW_ASSERT(Condition, ...)                                                            \
  do {                                                                                        \
    if (!(Condition)) [[unlikely]] {                                                          \
      ::wlw::detail::AssertFailed(#Condition, __FILE__, __LINE__, WLW_DETAIL_FORMAT(__VA_ARGS__));            \
    }                                                                                         \
  } while (0)
#else
// Not evaluated, but still compiled (so it can't rot) and its variables count as used.
#define WLW_ASSERT(Condition, ...) \
  do {                             \
    (void)sizeof(!(Condition));    \
  } while (0)
#endif

#define WLW_VERIFY(Condition, ...) \
  (static_cast<bool>(Condition) ? true : ::wlw::detail::VerifyFailed(#Condition, __FILE__, __LINE__, WLW_DETAIL_FORMAT(__VA_ARGS__)))

#define WLW_UNREACHABLE(...) ::wlw::detail::Unreachable(__FILE__, __LINE__, WLW_DETAIL_FORMAT(__VA_ARGS__))
