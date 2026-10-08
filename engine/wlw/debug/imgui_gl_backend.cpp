// Compiles Dear ImGui's OpenGL3 renderer backend against the engine's GL instead of its own
// loader: glad's GL 3.3 on desktop, the system GLES 3.0 headers on mobile. (Engine rule: GL
// comes from wlw/rendering/gl.h only.)
#include "wlw/rendering/gl.h"

#if defined(WLW_GLES)
#define IMGUI_IMPL_OPENGL_ES3
#else
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM  // functions already loaded by glad
#endif

#include "imgui_impl_opengl3.cpp"
