cmake_minimum_required(VERSION 3.21)

project(WLWengine VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WLWHelpers)

add_subdirectory(third_party)
add_subdirectory(engine)

# ---- Samples (small games that exercise the engine; desktop only) ----
# Built only when the engine is the top-level project, so games that include the engine as a
# submodule don't compile them.
if(NOT ANDROID)
  option(WLW_BUILD_SAMPLES "Build the sample games and their tests" ${PROJECT_IS_TOP_LEVEL})
  if(WLW_BUILD_SAMPLES)
    enable_testing()
    add_subdirectory(samples)
  endif()
endif()
