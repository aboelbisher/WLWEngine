# ---------------------------------------------------------------------------------------------
# Platform selection. Everything except the platform layer (wlw/platform/<os>/) is shared.
# ---------------------------------------------------------------------------------------------
if(ANDROID)
  set(WLW_PLATFORM "android")
elseif(IOS)
  message(FATAL_ERROR "WLWengine: the iOS platform layer isn't written yet.")
else()
  set(WLW_PLATFORM "glfw")  # Windows, macOS, Linux
endif()
message(STATUS "WLWengine platform: ${WLW_PLATFORM}")

# ---- glm (math, header-only) ----
# Desktop gets it from vcpkg (vcpkg.json). Builds without vcpkg (Android via Gradle) fetch it.
find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
  include(FetchContent)
  set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
  set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glm)
endif()

add_library(wlw STATIC
  wlw/wlw.h
  wlw/wlw.cpp

  wlw/anim/ease.h
  wlw/anim/tween.h

  wlw/app/application.cpp
  wlw/app/application.h
  wlw/app/entry_point.h

  wlw/audio/audio.cpp
  wlw/audio/audio.h
  wlw/audio/miniaudio_impl.cpp
  wlw/audio/mixer.cpp
  wlw/audio/mixer.h
  wlw/app/screen.cpp
  wlw/app/screen.h

  wlw/core/collision.h
  wlw/core/color.h
  wlw/core/fixed_timestep.h
  wlw/core/log.cpp
  wlw/core/log.h
  wlw/core/logger.h
  wlw/core/mesh.h
  wlw/core/model.h
  wlw/core/vector2.h
  wlw/core/vector3.h
  wlw/core/vertex_2d.h
  wlw/core/vertex_3d.h

  wlw/fx/particle_system.cpp
  wlw/fx/particle_system.h

  wlw/input/input.cpp
  wlw/input/input.h

  wlw/platform/platform.h

  wlw/rendering/gl.h
  wlw/rendering/gl_index_buffer.h
  wlw/rendering/gl_instance_buffer.h
  wlw/rendering/gl_vertex_buffer.h
  wlw/rendering/index_buffer.h
  wlw/rendering/instance_buffer.h
  wlw/rendering/material.h
  wlw/rendering/material_gl.cpp
  wlw/rendering/render_device.h
  wlw/rendering/render_device_gl.cpp
  wlw/rendering/rendering_driver.h
  wlw/rendering/rendering_driver_gl.cpp
  wlw/rendering/shaders/basic_shaders.h
  wlw/rendering/shaders/skybox_shaders.h
  wlw/rendering/texture.cpp
  wlw/rendering/texture.h
  wlw/rendering/vertex_buffer.h

  wlw/scene/camera_3d.h
  wlw/scene/follow_camera.h
  wlw/scene/fps_camera.cpp
  wlw/scene/fps_camera.h
  wlw/scene/node.h
  wlw/scene/orthographic_camera.h
  wlw/scene/triangle_2d.cpp
  wlw/scene/triangle_2d.h
  wlw/scene/window.h

  wlw/ui/canvas.cpp
  wlw/ui/canvas.h
  wlw/ui/font.cpp
  wlw/ui/font.h
  wlw/ui/ui_renderer.h
  wlw/ui/ui_renderer_gl.cpp

  wlw/utils/gltf_loader.cpp
  wlw/utils/gltf_loader.h
  wlw/utils/image_loader.cpp
  wlw/utils/image_loader.h
  wlw/utils/libs_impl.cpp
  wlw/utils/primitives.h
  wlw/utils/texture_manager.h
)
add_library(wlw::wlw ALIAS wlw)

# Games include engine headers as "wlw/scene/window.h" etc.
target_include_directories(wlw PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(wlw PUBLIC cxx_std_20)
# On Android the engine (a static library) is linked into the game's shared library, which
# requires position-independent code. Harmless on desktop.
set_target_properties(wlw PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(wlw PUBLIC WLW_RENDERER_GL)  # the OpenGL / OpenGL ES backend
target_link_libraries(wlw
  PUBLIC  glm::glm
  PRIVATE wlw_tinygltf wlw_stb wlw_miniaudio)

if(WLW_PLATFORM STREQUAL "glfw")
  # Desktop: GLFW window + OpenGL 3.3 core loaded by glad.
  find_package(glfw3 CONFIG REQUIRED)
  target_sources(wlw PRIVATE wlw/platform/glfw/glfw_platform.cpp)
  target_compile_definitions(wlw PUBLIC WLW_PLATFORM_GLFW GLFW_INCLUDE_NONE)
  target_link_libraries(wlw PUBLIC wlw_glad PRIVATE glfw)
  if(WIN32)
    target_link_libraries(wlw PUBLIC opengl32)
  endif()

elseif(WLW_PLATFORM STREQUAL "android")
  # Android: NativeActivity via android_native_app_glue (ships with the NDK), EGL + GLES 3.0.
  add_library(wlw_native_app_glue STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
  target_include_directories(wlw_native_app_glue SYSTEM PUBLIC "${ANDROID_NDK}/sources/android/native_app_glue")
  target_link_libraries(wlw_native_app_glue PUBLIC android log)
  set_target_properties(wlw_native_app_glue PROPERTIES POSITION_INDEPENDENT_CODE ON)

  target_sources(wlw PRIVATE wlw/platform/android/android_platform.cpp)
  target_compile_definitions(wlw PUBLIC WLW_PLATFORM_ANDROID WLW_GLES)
  target_link_libraries(wlw PUBLIC wlw_native_app_glue EGL GLESv3 android log)
  target_link_libraries(wlw PRIVATE dl)  # miniaudio loads AAudio / OpenSL ES at runtime
endif()

# ---- Debug UI (Dear ImGui) ----
# On by default; off by default in release-type builds, so it never ships by accident.
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
  set(_wlw_debug_ui_default OFF)
else()
  set(_wlw_debug_ui_default ON)
endif()
option(WLW_DEBUG_UI "Build the in-game debug UI (Dear ImGui)" ${_wlw_debug_ui_default})
if(WLW_DEBUG_UI)
  target_sources(wlw PRIVATE
    wlw/debug/debug_ui.cpp
    wlw/debug/debug_ui.h
    wlw/debug/imgui_gl_backend.cpp)
  target_compile_definitions(wlw PUBLIC WLW_DEBUG_UI=1)
  target_link_libraries(wlw PUBLIC wlw_imgui)  # games use the ImGui API in OnDebugUI()
else()
  target_compile_definitions(wlw PUBLIC WLW_DEBUG_UI=0)
endif()
message(STATUS "WLWengine debug UI: ${WLW_DEBUG_UI}")

wlw_set_warnings(wlw)

# Keep the folder structure in IDEs (VS / Xcode / Android Studio)
get_target_property(_wlw_sources wlw SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_wlw_sources})
