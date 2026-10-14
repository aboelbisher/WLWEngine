# Fails when a WLW_DEPRECATED shim (core/deprecated.h) has outlived its one release, or names a version that
# isn't the current or the next one. A shim marked 0.3 is fine while project(VERSION) is 0.2.x (written for the
# next release) or 0.3.x (shipped in it); bumping to 0.4.0 makes this fail until it's deleted.
#   cmake -P cmake/CheckDeprecations.cmake                        engine/ and kit/, version from CMakeLists.txt
#   cmake -DDIRS=<dir;dir> -DVERSION=<x.y.z> -P cmake/CheckDeprecations.cmake   (the script's own tests)
# CTest runs it as the test "deprecations".
cmake_minimum_required(VERSION 3.21)

get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED VERSION)
  file(READ "${root}/CMakeLists.txt" top)
  if(NOT top MATCHES "project\\(WLWengine VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "CheckDeprecations: no project(WLWengine VERSION x.y.z) in ${root}/CMakeLists.txt")
  endif()
  set(VERSION "${CMAKE_MATCH_1}")
endif()
if(NOT DEFINED DIRS)
  set(DIRS "${root}/engine" "${root}/kit")
endif()
if(NOT VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.[0-9]+$")
  message(FATAL_ERROR "CheckDeprecations: VERSION=\"${VERSION}\" isn't x.y.z")
endif()
set(major "${CMAKE_MATCH_1}")
set(minor "${CMAKE_MATCH_2}")
math(EXPR next_minor "${minor} + 1")
math(EXPR next_major "${major} + 1")
set(allowed "${major}.${minor}" "${major}.${next_minor}" "${next_major}.0")

set(files "")
foreach(dir IN LISTS DIRS)
  file(GLOB_RECURSE found "${dir}/*.h" "${dir}/*.hpp" "${dir}/*.cpp")
  list(APPEND files ${found})
endforeach()
list(FILTER files EXCLUDE REGEX "/core/deprecated\\.h$")  # the macro's own documentation

set(count 0)
set(problems "")
foreach(path IN LISTS files)
  file(STRINGS "${path}" lines REGEX "WLW_DEPRECATED\\(")
  foreach(line IN LISTS lines)
    if(NOT line MATCHES "WLW_DEPRECATED\\(([0-9]+)\\.([0-9]+)[ ,]")
      string(APPEND problems "\n  ${path}: no major.minor version: ${line}")
      continue()
    endif()
    math(EXPR count "${count} + 1")
    set(marked "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
    if(NOT marked IN_LIST allowed)
      string(APPEND problems "\n  ${path}: deprecated in ${marked}, the engine is ${VERSION}")
    endif()
  endforeach()
endforeach()

if(NOT problems STREQUAL "")
  message(FATAL_ERROR "WLW_DEPRECATED shims must name the current or the next release (${allowed}). Delete "
    "expired ones (CHANGELOG.md: Removed); mark new ones with the next release:${problems}")
endif()
message(STATUS "Deprecations: ${count} shim(s), all within one release of ${VERSION}")
