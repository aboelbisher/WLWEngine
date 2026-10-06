#pragma once

#include <sstream>

#include "wlw/core/log.h"

// FAIL_IF(condition, message): logs an error (with file/line) and evaluates to true when
// `condition` holds. `message` may use stream syntax: FAIL_IF(!img, "can't load " << path).
#define FAIL_IF(CONDITION, MESSAGE)                                                             \
  ([&]() -> bool {                                                                              \
    if ((CONDITION)) {                                                                          \
      std::ostringstream wlw_fail_message_;                                                     \
      wlw_fail_message_ << MESSAGE;                                                             \
      ::wlw::Log(::wlw::LogLevel::Error, "%s\n  Condition: %s\n  At: %s:%d",                    \
                 wlw_fail_message_.str().c_str(), #CONDITION, __FILE__, __LINE__);              \
      return true;                                                                              \
    }                                                                                           \
    return false;                                                                               \
  }())
