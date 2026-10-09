cmake_minimum_required(VERSION 3.21)

project(WLWengine VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WLWHelpers)

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(kit)

# ---- Samples and unit tests (desktop only) ----
# Built only when the engine is the top-level project, so games that include the engine as a
# submodule don't compile them.
if(NOT ANDROID)
  option(WLW_BUILD_SAMPLES "Build the sample games and their tests" ${PROJECT_IS_TOP_LEVEL})
  option(WLW_BUILD_TESTS "Build the engine unit tests" ${PROJECT_IS_TOP_LEVEL})
  if(WLW_BUILD_SAMPLES OR WLW_BUILD_TESTS)
    enable_testing()
  endif()
  if(WLW_BUILD_TESTS)
    add_subdirectory(tests)
  endif()
  if(WLW_BUILD_SAMPLES)
    add_subdirectory(samples)
  endif()
endif()
