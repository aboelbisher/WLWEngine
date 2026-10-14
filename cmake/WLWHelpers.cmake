# Helpers available to the engine and to any game that does add_subdirectory(<engine>).

# wlw_add_assets(<target> <assets_dir>)
# Copies the contents of <assets_dir> next to the target's executable after every build,
# and makes IDE debuggers start in that folder, so relative asset paths work everywhere.
function(wlw_add_assets target assets_dir)
  get_filename_component(assets_dir "${assets_dir}" ABSOLUTE)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(copy_cmd copy_directory_if_different)
  else()
    set(copy_cmd copy_directory)
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E ${copy_cmd} "${assets_dir}" "$<TARGET_FILE_DIR:${target}>"
    COMMENT "Copying assets for ${target}"
    VERBATIM)
  set_target_properties(${target} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
    XCODE_SCHEME_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>")
endfunction()

# wlw_enable_sanitizers() - WLW_SANITIZE="address;undefined" (or one of them) builds every target after
# this call with those sanitizers (clang and gcc; MSVC supports only address). Errors stop the program, so
# a test that triggers one fails. Off by default; the linux-asan preset and CI turn it on.
macro(wlw_enable_sanitizers)
  set(WLW_SANITIZE "" CACHE STRING "Sanitizers to build with: address;undefined (empty = none)")
  if(WLW_SANITIZE)
    if(MSVC)
      if(NOT WLW_SANITIZE STREQUAL "address")
        message(FATAL_ERROR "WLW_SANITIZE: MSVC supports only address")
      endif()
      add_compile_options(/fsanitize=address)
    else()
      list(JOIN WLW_SANITIZE "," _wlw_sanitizers)
      add_compile_options(-fsanitize=${_wlw_sanitizers} -fno-sanitize-recover=all -fno-omit-frame-pointer)
      add_link_options(-fsanitize=${_wlw_sanitizers})
    endif()
    message(STATUS "WLWengine sanitizers: ${WLW_SANITIZE}")
  endif()
endmacro()

# wlw_set_warnings(<target>) - common compiler settings for engine + game targets.
function(wlw_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3 /utf-8 /MP)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endfunction()

# wlw_add_game(<target> SOURCES <files...> [ASSETS <dir>])
# Creates a game target the right way for the current platform:
#   desktop: an executable (assets copied next to it)
#   Android: a shared library lib<target>.so that NativeActivity loads (assets are packed into
#            the APK by Gradle instead; see the game's android/ project)
function(wlw_add_game target)
  cmake_parse_arguments(GAME "" "ASSETS" "SOURCES" ${ARGN})
  if(ANDROID)
    add_library(${target} SHARED ${GAME_SOURCES})
    # The glue's ANativeActivity_onCreate lives in a static library and nothing references it;
    # force it in, otherwise the linker drops it and Android can't start the app.
    target_link_options(${target} PRIVATE "SHELL:-u ANativeActivity_onCreate")
  else()
    add_executable(${target} ${GAME_SOURCES})
    if(GAME_ASSETS)
      wlw_add_assets(${target} "${GAME_ASSETS}")
    endif()
  endif()
  target_link_libraries(${target} PRIVATE wlw::wlw)
  wlw_set_warnings(${target})
endfunction()
