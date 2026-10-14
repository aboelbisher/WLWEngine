// Desktop precise sleep for the frame limiter (platform.h: PreciseSleep). Its own file because
// <windows.h> doesn't mix well with GLFW and glad in glfw_platform.cpp.
#ifdef WLW_PLATFORM_GLFW

#include <chrono>
#include <thread>

#include "wlw/platform/platform.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002  // Windows 10 1803+
#endif
#endif

namespace wlw::platform {

void PreciseSleep(double seconds) {
  if (seconds <= 0.0) return;
  using Clock = std::chrono::steady_clock;
  const Clock::time_point end =
      Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
  // The OS wakes us up to ~0.5 ms late: sleep until 1 ms before the end, spin the rest.
  constexpr double kSpinSeconds = 0.001;
  if (seconds > kSpinSeconds) {
    const double coarse = seconds - kSpinSeconds;
#if defined(_WIN32)
    // Sleep() has the system timer's 15.6 ms granularity; a high-resolution waitable timer doesn't
    // (and, unlike timeBeginPeriod, doesn't raise the timer rate for the whole system). Where it
    // doesn't exist (Windows before 10 1803) the whole wait is the spin below.
    thread_local const HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer != nullptr) {
      LARGE_INTEGER due;
      due.QuadPart = -static_cast<LONGLONG>(coarse * 1e7);  // negative = relative, in 100 ns units
      if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
    }
#else
    std::this_thread::sleep_for(std::chrono::duration<double>(coarse));  // nanosleep: accurate to ~0.1 ms
#endif
  }
  while (Clock::now() < end) std::this_thread::yield();
}

}  // namespace wlw::platform

#endif  // WLW_PLATFORM_GLFW
