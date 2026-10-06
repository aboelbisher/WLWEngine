cmake_minimum_required(VERSION 3.21)

project(WLWengine VERSION 0.1.0 LANGUAGES C CXX)

# Examples build only when the engine is the top-level project (i.e. not when a game pulls it in).
option(WLW_BUILD_EXAMPLES "Build the sample games in examples/" ${PROJECT_IS_TOP_LEVEL})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WLWHelpers)

add_subdirectory(third_party)
add_subdirectory(engine)

if(WLW_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
