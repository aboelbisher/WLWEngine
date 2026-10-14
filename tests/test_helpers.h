#pragma once

// Shared by the engine's unit tests (one doctest binary, wlw_tests; main in test_main.cpp).
// doctest's CHECK can't decompose && / || / ?:, so compound conditions go in double parentheses:
// CHECK((a && b)).
#include <cmath>

#include <doctest.h>

// Absolute tolerance (doctest::Approx is relative, which is wrong near zero).
inline bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }
