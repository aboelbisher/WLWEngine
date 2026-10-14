# ---- glad: OpenGL 3.3 core function loader (desktop only; GLES links its functions directly) ----
if(NOT ANDROID AND NOT IOS)
  add_library(wlw_glad STATIC glad/src/glad.c)
  target_include_directories(wlw_glad SYSTEM PUBLIC glad/include)
  set_target_properties(wlw_glad PROPERTIES FOLDER "third_party")
endif()

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
