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

add_library(wlw STATIC
  wlw/wlw.h
  wlw/wlw.cpp
  wlw/version.h.in

  wlw/anim/ease.h
  wlw/anim/shake.h
  wlw/anim/tween.h

  wlw/app/application.cpp
  wlw/app/application.h
  wlw/app/entry_point.h
  wlw/app/quality.cpp
  wlw/app/quality.h

  wlw/audio/audio.cpp
  wlw/audio/audio.h
  wlw/audio/miniaudio_impl.cpp
  wlw/audio/mixer.cpp
  wlw/audio/mixer.h
  wlw/app/screen.cpp
  wlw/app/screen.h
  wlw/app/system.h
  wlw/app/time.h

  wlw/core/collision.h
  wlw/core/color.cpp
  wlw/core/color.h
  wlw/core/fixed_timestep.h
  wlw/core/frame_pacing.cpp
  wlw/core/frame_pacing.h
  wlw/core/time_scale.h
  wlw/core/type_name.h
  wlw/core/assert.cpp
  wlw/core/assert.h
  wlw/core/build_info.cpp
  wlw/core/build_info.h
  wlw/app/engine_cvars.h
  wlw/core/console.cpp
  wlw/core/console.h
  wlw/core/cvar.cpp
  wlw/core/cvar.h
  wlw/core/deprecated.h
  wlw/core/geometry.h
  wlw/core/git_info.h
  wlw/core/log.cpp
  wlw/core/log.h
  wlw/core/math.h
  wlw/core/mesh.h
  wlw/core/normal_matrix.h
  wlw/core/random.h
  wlw/core/rotation.cpp
  wlw/core/rotation.h
  wlw/core/model.h
  wlw/core/profile.cpp
  wlw/core/profile.h
  wlw/core/vector2.h
  wlw/core/vector3.h
  wlw/core/vertex_3d.h

  wlw/fx/flow_particles.cpp
  wlw/fx/flow_particles.h
  wlw/fx/particle_system.cpp
  wlw/fx/particle_system.h

  wlw/math/exact_cover.cpp
  wlw/math/bit_grid.h
  wlw/math/exact_cover.h
  wlw/math/game_search.h
  wlw/math/gf2.h
  wlw/math/gf2.cpp

  wlw/input/input.cpp
  wlw/input/input.h

  wlw/platform/bridge.h
  wlw/platform/bridge.cpp
  wlw/platform/launch_arguments.cpp
  wlw/platform/platform.h

  wlw/rendering/blend_mode.h
  wlw/rendering/gl.h
  wlw/rendering/gl_blend.h
  wlw/rendering/frame_uniforms.h
  wlw/rendering/gl_index_buffer.h
  wlw/rendering/gl_program.cpp
  wlw/rendering/gl_program.h
  wlw/rendering/gl_gpu_queries.cpp
  wlw/rendering/gl_gpu_queries.h
  wlw/rendering/gl_gpu_profile.cpp
  wlw/rendering/gl_gpu_profile.h
  wlw/rendering/gl_resources.cpp
  wlw/rendering/gpu_timer.cpp
  wlw/rendering/gpu_timer.h
  wlw/rendering/gl_resources.h
  wlw/rendering/gpu_resource.h
  wlw/rendering/gl_shader.cpp
  wlw/rendering/gl_state_cache.h
  wlw/rendering/gl_shader.h
  wlw/rendering/gl_instance_buffer.h
  wlw/rendering/gl_vertex_buffer.h
  wlw/rendering/glsl_header.cpp
  wlw/rendering/glsl_header.h
  wlw/rendering/index_buffer.h
  wlw/rendering/instance_buffer.cpp
  wlw/rendering/instance_buffer.h
  wlw/rendering/material.h
  wlw/rendering/mesh_variant.h
  wlw/rendering/post_pass.cpp
  wlw/rendering/post_pass.h
  wlw/rendering/render_device.h
  wlw/rendering/render_device_gl.cpp
  wlw/rendering/render_list.h
  wlw/rendering/render_target.h
  wlw/rendering/rendering_driver.h
  wlw/rendering/rendering_driver_gl.cpp
  wlw/rendering/scene_gather.cpp
  wlw/rendering/scene_gather.h
  wlw/rendering/shader.cpp
  wlw/rendering/shader.h
  wlw/rendering/shader_library.cpp
  wlw/rendering/shader_library.h
  wlw/rendering/shader_material.h
  wlw/rendering/sky.h
  wlw/rendering/shader_preprocessor.cpp
  wlw/rendering/shader_preprocessor.h
  wlw/rendering/texture.cpp
  wlw/rendering/texture.h
  wlw/rendering/vertex_buffer.h

  wlw/scene/camera_3d.cpp
  wlw/scene/camera_3d.h
  wlw/scene/camera_controllers.cpp
  wlw/scene/camera_controllers.h
  wlw/scene/geometry_instance.cpp
  wlw/scene/geometry_instance.h
  wlw/scene/light.h
  wlw/scene/node.h
  wlw/scene/node_3d.cpp
  wlw/scene/orthographic_camera.h
  wlw/scene/post_effects.cpp
  wlw/scene/post_effects.h
  wlw/scene/scene.cpp
  wlw/scene/scene.h
  wlw/scene/shadow_map.h
  wlw/scene/sprite_batch.h
  wlw/scene/viewport.h
  wlw/scene/window.h

  wlw/ui/canvas.cpp
  wlw/ui/canvas.h
  wlw/ui/font.cpp
  wlw/ui/font.h
  wlw/ui/icons.cpp
  wlw/ui/icons.h
  wlw/ui/layout.h
  wlw/ui/theme.h
  wlw/ui/ui_context.h
  wlw/ui/ui_renderer.h
  wlw/ui/ui_renderer_gl.cpp
  wlw/ui/widgets.cpp
  wlw/ui/widgets.h

  wlw/utils/gltf_loader.cpp
  wlw/utils/gltf_loader.h
  wlw/utils/image_loader.cpp
  wlw/utils/image_loader.h
  wlw/utils/libs_impl.cpp
  wlw/utils/primitives.h
  wlw/utils/texture_manager.h
  wlw/utils/textures.h
)
add_library(wlw::wlw ALIAS wlw)

# ---- Shaders: engine/wlw/rendering/shaders/**.{glsl,vert,frag}, embedded in the binary ----
# cmake/EmbedShaders.cmake turns them into a generated source (rendering/shader_library.h), so
# nothing is read from disk or the APK; editing a shader file re-runs it on the next build.
set(_wlw_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/wlw/rendering/shaders")
file(GLOB_RECURSE _wlw_shader_files CONFIGURE_DEPENDS
  "${_wlw_shader_dir}/*.glsl" "${_wlw_shader_dir}/*.vert" "${_wlw_shader_dir}/*.frag")
set(_wlw_shaders_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/wlw_shader_files.cpp")
add_custom_command(
  OUTPUT "${_wlw_shaders_cpp}"
  COMMAND "${CMAKE_COMMAND}" "-DSHADER_DIR=${_wlw_shader_dir}" "-DOUTPUT=${_wlw_shaders_cpp}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/EmbedShaders.cmake"
  DEPENDS ${_wlw_shader_files} "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/EmbedShaders.cmake"
  COMMENT "Embedding engine shaders"
  VERBATIM)
set_source_files_properties(${_wlw_shader_files} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(wlw PRIVATE "${_wlw_shaders_cpp}" ${_wlw_shader_files})

# ---- Version (wlw/version.h) and build info (core/build_info.h) ----
# version.h comes from project(VERSION) in the top-level CMakeLists.txt; minor and patch stay below 100
# (WLW_VERSION packs them into one number).
if(WLWengine_VERSION_MINOR GREATER 99 OR WLWengine_VERSION_PATCH GREATER 99)
  message(FATAL_ERROR "WLWengine: minor and patch versions must stay below 100 (${WLWengine_VERSION})")
endif()
set(_wlw_generated_include "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(wlw/version.h.in "${_wlw_generated_include}/wlw/version.h" @ONLY)
set_source_files_properties(wlw/version.h.in PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(wlw PUBLIC "${_wlw_generated_include}")
message(STATUS "WLWengine version: ${WLWengine_VERSION}")

# The git commit is read on every build (a commit made after configuring still shows), but the generated source
# only changes when the commit or the dirty flag does. Also run once now, so the file exists for IDEs.
find_package(Git QUIET)
set(_wlw_git_info_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/wlw_git_info.cpp")
set(_wlw_git_info_args "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.." "-DOUTPUT=${_wlw_git_info_cpp}"
  "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}")
execute_process(COMMAND "${CMAKE_COMMAND}" ${_wlw_git_info_args} "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GitInfo.cmake")
add_custom_target(wlw_git_info
  COMMAND "${CMAKE_COMMAND}" ${_wlw_git_info_args} "-DBUILD_TYPE=$<CONFIG>"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GitInfo.cmake"
  BYPRODUCTS "${_wlw_git_info_cpp}"
  COMMENT "Reading the engine's git commit"
  VERBATIM)
add_dependencies(wlw wlw_git_info)
target_sources(wlw PRIVATE "${_wlw_git_info_cpp}")

# Deprecated shims (core/deprecated.h): ON compiles them all out, to check a game no longer uses any.
option(WLW_NO_DEPRECATED "Compile out the engine's deprecated API shims" OFF)
target_compile_definitions(wlw PUBLIC "WLW_NO_DEPRECATED=$<BOOL:${WLW_NO_DEPRECATED}>")

# Games include engine headers as "wlw/scene/window.h" etc.
target_include_directories(wlw PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(wlw PUBLIC cxx_std_20)
# On Android the engine (a static library) is linked into the game's shared library, which
# requires position-independent code. Harmless on desktop.
set_target_properties(wlw PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(wlw PUBLIC WLW_RENDERER_GL)  # the OpenGL / OpenGL ES backend
target_link_libraries(wlw
  PUBLIC  wlw_glm
  PRIVATE wlw_tinygltf wlw_stb wlw_miniaudio)

if(WLW_PLATFORM STREQUAL "glfw")
  # Desktop: GLFW window + OpenGL 3.3 core loaded by glad.
  find_package(glfw3 CONFIG REQUIRED)
  target_sources(wlw PRIVATE wlw/platform/glfw/glfw_platform.cpp wlw/platform/glfw/glfw_debugger.cpp
                             wlw/platform/glfw/glfw_sleep.cpp)
  target_compile_definitions(wlw PUBLIC WLW_PLATFORM_GLFW GLFW_INCLUDE_NONE)
  target_link_libraries(wlw PUBLIC wlw_glad PRIVATE glfw)
  if(WIN32)
    target_link_libraries(wlw PUBLIC opengl32)
  elseif(UNIX AND NOT APPLE)
    # Linux: miniaudio loads ALSA / PulseAudio at runtime and runs its own thread
    find_package(Threads REQUIRED)
    target_link_libraries(wlw PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
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
  target_link_libraries(wlw PRIVATE wlw_swappy)  # frame pacing (third_party/swappy)
  target_link_libraries(wlw PRIVATE dl)  # miniaudio loads AAudio / OpenSL ES at runtime
endif()

# ---- Asserts (core/assert.h) ----
# WLW_ASSERT is on in debug builds and compiled out in release; this keeps it in release builds too
# (e.g. a test build for testers).
option(WLW_ENABLE_ASSERTS "Keep WLW_ASSERT in release builds" OFF)
if(WLW_ENABLE_ASSERTS)
  target_compile_definitions(wlw PUBLIC WLW_ENABLE_ASSERTS)
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
    wlw/debug/console_window.cpp
    wlw/debug/console_window.h
    wlw/debug/cvar_panel.cpp
    wlw/debug/cvar_panel.h
    wlw/debug/debug_ui.cpp
    wlw/debug/debug_ui.h
    wlw/debug/engine_panel.cpp
    wlw/debug/engine_panel.h
    wlw/debug/imgui_gl_backend.cpp)
  target_compile_definitions(wlw PUBLIC WLW_DEBUG_UI=1)
  target_link_libraries(wlw PUBLIC wlw_imgui)  # games use the ImGui API in OnDebugUI()
else()
  target_compile_definitions(wlw PUBLIC WLW_DEBUG_UI=0)
endif()
message(STATUS "WLWengine debug UI: ${WLW_DEBUG_UI}")

# ---- Profiling (core/profile.h; the option is in the top-level CMakeLists) ----
if(WLW_PROFILE)
  target_compile_definitions(wlw PUBLIC WLW_PROFILE=1)
  target_link_libraries(wlw PUBLIC wlw_tracy)  # games' zones use Tracy's headers too
else()
  target_compile_definitions(wlw PUBLIC WLW_PROFILE=0)
endif()
message(STATUS "WLWengine profiling (Tracy): ${WLW_PROFILE}")

wlw_set_warnings(wlw)
# These files only compile third-party implementations (miniaudio + stb_vorbis, stb_image, tinygltf): their
# warnings aren't ours, and MSVC reports some (C4701) while generating code, past /external:W0.
set_source_files_properties(wlw/audio/miniaudio_impl.cpp wlw/utils/libs_impl.cpp
  PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>" SKIP_PRECOMPILE_HEADERS ON)

# ---- Precompiled headers: the standard library and glm headers most engine files include ----
option(WLW_PCH "Precompile the engine's common headers" ON)
if(WLW_PCH)
  target_precompile_headers(wlw PRIVATE
    <algorithm> <array> <cmath> <cstddef> <cstdint> <cstdio> <functional> <memory> <optional> <span> <string>
    <string_view> <unordered_map> <utility> <vector>
    <glm/glm.hpp> <glm/gtc/matrix_transform.hpp> <glm/gtc/type_ptr.hpp>)
endif()

# Keep the folder structure in IDEs (VS / Xcode / Android Studio)
get_target_property(_wlw_sources wlw SOURCES)
list(FILTER _wlw_sources EXCLUDE REGEX "/generated/")  # outside the source tree
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_wlw_sources})
