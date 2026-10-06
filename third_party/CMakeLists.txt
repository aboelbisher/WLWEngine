# ---- glad (OpenGL 3.3 core loader) ----
add_library(wlw_glad STATIC glad/src/glad.c)
target_include_directories(wlw_glad SYSTEM PUBLIC glad/include)
set_target_properties(wlw_glad PROPERTIES FOLDER "third_party")

# ---- header-only: stb, tinygltf (+ nlohmann json) ----
add_library(wlw_stb INTERFACE)
target_include_directories(wlw_stb SYSTEM INTERFACE stb)

add_library(wlw_tinygltf INTERFACE)
target_include_directories(wlw_tinygltf SYSTEM INTERFACE tinygltf)
target_link_libraries(wlw_tinygltf INTERFACE wlw_stb)
