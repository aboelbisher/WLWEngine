cmake_minimum_required(VERSION 3.21)

project(WLWengine VERSION 0.3.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WLWHelpers)
wlw_enable_sanitizers()  # WLW_SANITIZE: before any target, so everything is instrumented

# Profiling (wlw/core/profile.h): Tracy + ATrace on Android. Off by default in every build type, so it never
# ships by accident; the *-profile presets turn it on in an optimized build with symbols.
option(WLW_PROFILE "Build the Tracy profiler client and the WLW_PROFILE_* zones in" OFF)

# Debug builds of a game: the engine, kit and third_party compile optimized with debug info (-O2 -g), the
# game's own code stays -O0 (like Unreal's DebugGame). An -O0 engine is ~10x slower on phones. On by default
# when the engine is a game's submodule; off when the engine is the top-level project (engine work steps
# through engine code). Only these three directories change: tests, samples and benchmarks keep their flags.
if(PROJECT_IS_TOP_LEVEL)
  set(_wlw_optimize_default OFF)
else()
  set(_wlw_optimize_default ON)
endif()
option(WLW_OPTIMIZE_ENGINE_IN_DEBUG "Debug builds: compile the engine, kit and third_party with -O2 -g"
  ${_wlw_optimize_default})
set(_wlw_saved_c_flags_debug "${CMAKE_C_FLAGS_DEBUG}")
set(_wlw_saved_cxx_flags_debug "${CMAKE_CXX_FLAGS_DEBUG}")
if(WLW_OPTIMIZE_ENGINE_IN_DEBUG)
  wlw_optimize_debug_flags()
  message(STATUS "WLWengine: engine optimized in Debug builds (${CMAKE_CXX_FLAGS_DEBUG})")
endif()

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(kit)

set(CMAKE_C_FLAGS_DEBUG "${_wlw_saved_c_flags_debug}")
set(CMAKE_CXX_FLAGS_DEBUG "${_wlw_saved_cxx_flags_debug}")

# ---- Samples and unit tests ----
# Built only when the engine is the top-level project, so games that include the engine as a
# submodule don't compile them. Tests run on desktop only; on Android the samples are off by default
# (CI turns them on to check that Snake compiles and links as a game library).
if(ANDROID)
  option(WLW_BUILD_SAMPLES "Build the sample games" OFF)
  set(WLW_BUILD_TESTS OFF)
else()
  option(WLW_BUILD_SAMPLES "Build the sample games and their tests" ${PROJECT_IS_TOP_LEVEL})
  option(WLW_BUILD_TESTS "Build the engine unit tests" ${PROJECT_IS_TOP_LEVEL})
  if(WLW_BUILD_SAMPLES OR WLW_BUILD_TESTS)
    enable_testing()
    include(third_party/doctest/doctest.cmake)  # doctest_discover_tests()
  endif()
endif()
if(WLW_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(WLW_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

# Microbenchmarks (benchmarks/, nanobench): desktop and Android (an executable run over adb).
option(WLW_BUILD_BENCHMARKS "Build the engine microbenchmarks (wlw_benchmarks)" ${PROJECT_IS_TOP_LEVEL})
if(WLW_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
