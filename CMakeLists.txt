cmake_minimum_required(VERSION 3.21)

project(WLWengine VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WLWHelpers)

add_subdirectory(third_party)
add_subdirectory(engine)
