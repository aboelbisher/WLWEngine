# ---- glad: OpenGL 3.3 core function loader (desktop only; GLES links its functions directly) ----
if(NOT ANDROID AND NOT IOS)
  add_library(wlw_glad STATIC glad/src/glad.c)
  target_include_directories(wlw_glad SYSTEM PUBLIC glad/include)
  set_target_properties(wlw_glad PROPERTIES FOLDER "third_party")
endif()

# ---- glm 1.0.1 (MIT / Happy Bunny; headers only): maths. Public through wlw::wlw, so games get it too ----
add_library(wlw_glm INTERFACE)
target_include_directories(wlw_glm SYSTEM INTERFACE glm)

# ---- header-only: stb, tinygltf (+ nlohmann json) ----
add_library(wlw_stb INTERFACE)
target_include_directories(wlw_stb SYSTEM INTERFACE stb)

add_library(wlw_tinygltf INTERFACE)
target_include_directories(wlw_tinygltf SYSTEM INTERFACE tinygltf)
target_link_libraries(wlw_tinygltf INTERFACE wlw_stb)

# nlohmann json on its own (it ships in the tinygltf folder): wlw::kit uses it for save files
add_library(wlw_json INTERFACE)
target_include_directories(wlw_json SYSTEM INTERFACE tinygltf)

# ---- Dear ImGui v1.92.9b (MIT; vendored core + OpenGL3 renderer backend): the debug UI ----
# Linked by the engine only when WLW_DEBUG_UI is on. The backend is compiled by the engine
# (wlw/debug/imgui_gl_backend.cpp) so it uses the engine's GL headers.
add_library(wlw_imgui STATIC
  imgui/imgui.cpp
  imgui/imgui_demo.cpp
  imgui/imgui_draw.cpp
  imgui/imgui_tables.cpp
  imgui/imgui_widgets.cpp)
target_include_directories(wlw_imgui SYSTEM PUBLIC imgui imgui/backends)
set_target_properties(wlw_imgui PROPERTIES FOLDER "third_party" POSITION_INDEPENDENT_CODE ON)

# ---- miniaudio v0.11.25 (public domain / MIT-0) + stb_vorbis: audio output and decoding ----
# Header-only; compiled once in the engine (wlw/audio/miniaudio_impl.cpp).
add_library(wlw_miniaudio INTERFACE)
target_include_directories(wlw_miniaudio SYSTEM INTERFACE miniaudio stb)

# ---- doctest v2.4.12 (MIT): the unit-test framework (header-only) ----
# doctest.cmake gives doctest_discover_tests(): every TEST_CASE becomes its own CTest test.
add_library(wlw_doctest INTERFACE)
target_include_directories(wlw_doctest SYSTEM INTERFACE doctest)

# ---- Tracy v0.14.1 (BSD-3; the client from its public/ folder): the profiler (wlw/core/profile.h) ----
# Only with WLW_PROFILE. TRACY_ON_DEMAND: nothing is recorded until a viewer connects, so a profile build runs
# at full speed without one. The viewer (tracy-profiler) must be the same version.
if(WLW_PROFILE)
  add_library(wlw_tracy STATIC tracy/TracyClient.cpp)
  target_include_directories(wlw_tracy SYSTEM PUBLIC tracy)
  target_compile_definitions(wlw_tracy PUBLIC TRACY_ENABLE TRACY_ON_DEMAND)
  set_target_properties(wlw_tracy PROPERTIES FOLDER "third_party" POSITION_INDEPENDENT_CODE ON)
  find_package(Threads REQUIRED)
  target_link_libraries(wlw_tracy PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_libraries(wlw_tracy PUBLIC ws2_32 dbghelp)
  elseif(ANDROID)
    target_link_libraries(wlw_tracy PUBLIC log)
  endif()
endif()

# ---- nanobench v4.3.11 (MIT): microbenchmarks (benchmarks/; header-only, compiled once in bench_main.cpp) ----
add_library(wlw_nanobench INTERFACE)
target_include_directories(wlw_nanobench SYSTEM INTERFACE nanobench)

# ---- Swappy 2.3.0-alpha01 (Apache-2.0; AGDK games-frame-pacing, OpenGL part): Android frame pacing ----
# Sources from AGDK's last commit (gamesdk main 044fd03, March 2025; the repo is archived since). The Java
# classes it calls (java/com/google/androidgamesdk) are compiled by the game's Gradle build from its source
# set, not embedded as a dex: ANDROIDGAMESDK_NO_BINARY_DEX_LINKAGE.
if(ANDROID)
  add_library(wlw_swappy STATIC
    swappy/src/common/ChoreographerFilter.cpp
    swappy/src/common/ChoreographerThread.cpp
    swappy/src/common/CpuInfo.cpp
    swappy/src/common/CPUTracer.cpp
    swappy/src/common/FrameStatistics.cpp
    swappy/src/common/Settings.cpp
    swappy/src/common/swappy_c.cpp
    swappy/src/common/SwappyCommon.cpp
    swappy/src/common/SwappyDisplayManager.cpp
    swappy/src/common/Thread.cpp
    swappy/src/opengl/EGL.cpp
    swappy/src/opengl/FrameStatisticsGL.cpp
    swappy/src/opengl/SwappyGL.cpp
    swappy/src/opengl/swappyGL_c.cpp
    swappy/src/gamesdk/system_utils.cpp)
  target_include_directories(wlw_swappy SYSTEM PUBLIC swappy/include)
  target_include_directories(wlw_swappy PRIVATE swappy/src/common swappy/src/opengl swappy/src/gamesdk)
  target_compile_definitions(wlw_swappy PRIVATE ANDROIDGAMESDK_NO_BINARY_DEX_LINKAGE)
  set_target_properties(wlw_swappy PROPERTIES FOLDER "third_party" POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(wlw_swappy PUBLIC android EGL GLESv3 log)
endif()
