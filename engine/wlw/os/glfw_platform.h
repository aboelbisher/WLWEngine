#pragma once

#ifdef WLW_USE_GLFW
#include <GLFW/glfw3.h>

namespace wlw::os {

// True between glfwInit() and glfwTerminate() (set by the rendering driver).
inline bool g_glfw_initialized = false;

// GPU resources (buffers, textures) may be released after the engine shut GLFW down,
// e.g. when a game keeps a mesh alive longer than the engine. Deleting GL objects then
// would call into a dead context, so their destructors check this first.
inline bool HasCurrentGLContext() {
  return g_glfw_initialized && glfwGetCurrentContext() != nullptr;
}

}  // namespace wlw::os
#endif  // WLW_USE_GLFW
