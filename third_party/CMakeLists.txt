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
