# ---- dependencies (provided by vcpkg via vcpkg.json, or any CMake package provider) ----
find_package(glm CONFIG REQUIRED)

if(ANDROID OR IOS)
  message(FATAL_ERROR "WLWengine: mobile platforms aren't implemented yet (needs a non-GLFW platform layer + GLES).")
endif()
find_package(glfw3 CONFIG REQUIRED)

add_library(wlw STATIC
  wlw/wlw.h
  wlw/wlw.cpp

  wlw/core/collision.h
  wlw/core/color.h
  wlw/core/logger.h
  wlw/core/mesh.h
  wlw/core/model.h
  wlw/core/vector2.h
  wlw/core/vector3.h
  wlw/core/vertex_2d.h
  wlw/core/vertex_3d.h

  wlw/os/input_engine.h
  wlw/os/input_engine_gl.cpp

  wlw/rendering/gl_index_buffer.h
  wlw/rendering/gl_vertex_buffer.h
  wlw/rendering/index_buffer.h
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
  wlw/scene/triangle_2d.cpp
  wlw/scene/triangle_2d.h
  wlw/scene/window.cpp
  wlw/scene/window.h

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

# Desktop backend: GLFW window + OpenGL 3.3 via glad.
target_compile_definitions(wlw PUBLIC WLW_USE_GLFW GLFW_INCLUDE_NONE)
target_link_libraries(wlw
  PUBLIC  glm::glm glfw wlw_glad
  PRIVATE wlw_tinygltf wlw_stb)
if(WIN32)
  target_link_libraries(wlw PUBLIC opengl32)
endif()

wlw_set_warnings(wlw)

# Keep the folder structure in IDEs (VS / Xcode)
get_target_property(_wlw_sources wlw SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_wlw_sources})
