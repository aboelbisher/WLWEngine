// Desktop debugger detection for asserts (platform.h: IsDebuggerAttached, BreakIntoDebugger). Its own
// file because <windows.h> doesn't mix well with GLFW and glad in glfw_platform.cpp.
#ifdef WLW_PLATFORM_GLFW

#include "wlw/platform/platform.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <csignal>
#include <sys/sysctl.h>
#include <unistd.h>
#else
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <string>
#endif

namespace wlw::platform {

#if defined(_WIN32)

bool IsDebuggerAttached() { return ::IsDebuggerPresent() != 0; }
void BreakIntoDebugger() { __debugbreak(); }

#elif defined(__APPLE__)

bool IsDebuggerAttached() {
  int request[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  kinfo_proc info{};
  size_t size = sizeof(info);
  if (sysctl(request, 4, &info, &size, nullptr, 0) != 0) return false;
  return (info.kp_proc.p_flag & P_TRACED) != 0;
}
void BreakIntoDebugger() { raise(SIGTRAP); }

#else  // Linux: a debugger (ptrace) shows as a non-zero TracerPid

bool IsDebuggerAttached() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("TracerPid:", 0) == 0) return std::atoi(line.c_str() + 10) != 0;
  }
  return false;
}
void BreakIntoDebugger() { raise(SIGTRAP); }

#endif

}  // namespace wlw::platform

#endif  // WLW_PLATFORM_GLFW
