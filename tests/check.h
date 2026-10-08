#pragma once

// A minimal test helper shared by the engine's unit tests: CHECK(cond) prints the failing
// condition and keeps going; main() returns TestResult() so CTest sees the failure.
#include <cmath>
#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                             \
  do {                                                          \
    if (!(cond)) {                                              \
      std::printf("  FAILED: %s (line %d)\n", #cond, __LINE__); \
      ++g_failures;                                             \
    }                                                           \
  } while (0)

inline bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

inline int TestResult(const char* name) {
  if (g_failures == 0) std::printf("All %s tests passed.\n", name);
  return g_failures == 0 ? 0 : 1;
}
