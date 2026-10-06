#include "wlw/core/log.h"

#include <cstdarg>
#include <cstdio>

#include "wlw/platform/platform.h"

namespace wlw {

void Log(LogLevel level, const char* format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  platform::WriteLog(level, buffer);
}

}  // namespace wlw
