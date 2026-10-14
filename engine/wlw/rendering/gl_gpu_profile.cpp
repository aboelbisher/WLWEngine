#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl_gpu_profile.h"

#if WLW_PROFILE

#include "wlw/core/cvar.h"
#include "wlw/core/log.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl_resources.h"

namespace wlw::rendering {

#if defined(WLW_GLES)
namespace gles_timer_query {
void(GL_APIENTRY* query_counter)(GLuint id, GLenum target) = nullptr;
void(GL_APIENTRY* get_query_object_iv)(GLuint id, GLenum pname, GLint* params) = nullptr;
void(GL_APIENTRY* get_query_object_ui64v)(GLuint id, GLenum pname, GLuint64* params) = nullptr;
}  // namespace gles_timer_query
#endif

namespace {
// A way out if a driver handles the timestamp queries badly. Measured on desktop NVIDIA: no net cost. Its driver
// thread makes the frame's first glGet wait (frame pacing, ms at a time), so with GPU zones that wait shows in
// tracy::GpuCtx::Collect, without them in "Screen framebuffer"; "Draw window" takes the same either way.
CVar<bool> cv_gpu_zones{"profile.gpu_zones", true, "GPU zones in the profiler (timestamp queries)"};
bool supported = false;  // this GPU has timestamps
bool made = false;       // Tracy's query context exists (made when a viewer first connects)
}  // namespace

void StartGpuProfiler() {
#if defined(WLW_GLES)
  if (!HasGLExtension("GL_EXT_disjoint_timer_query")) {
    WLW_LOG(LogRender, Info, "Profiler: no GPU zones (no EXT_disjoint_timer_query)");
    return;
  }
  using namespace gles_timer_query;
  query_counter = reinterpret_cast<decltype(query_counter)>(platform::GetGLProcAddress("glQueryCounterEXT"));
  get_query_object_iv =
      reinterpret_cast<decltype(get_query_object_iv)>(platform::GetGLProcAddress("glGetQueryObjectivEXT"));
  get_query_object_ui64v =
      reinterpret_cast<decltype(get_query_object_ui64v)>(platform::GetGLProcAddress("glGetQueryObjectui64vEXT"));
  if (!query_counter || !get_query_object_iv || !get_query_object_ui64v) {
    WLW_LOG(LogRender, Info, "Profiler: no GPU zones (EXT_disjoint_timer_query functions missing)");
    return;
  }
#endif
  // Some drivers (tiled GPUs especially) list the extension but give timestamps no bits: every zone would be 0.
  GLint bits = 0;
  glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
  while (glGetError() != GL_NO_ERROR) {
  }
  if (bits <= 0) {
    WLW_LOG(LogRender, Info, "Profiler: no GPU zones (the GPU's timestamps have no precision)");
    return;
  }
  supported = true;
}

bool GpuProfilerActive() { return made && cv_gpu_zones.Get(); }  // Tracy's zones also check that a viewer is connected

void CollectGpuProfiler() {
  if (!supported || !cv_gpu_zones.Get()) return;
  if (!made) {
    if (!TracyIsConnected) return;
    TracyGpuContext;
    TracyGpuContextName("GPU", 3);
    made = true;
    return;  // zones start with the next frame
  }
  TracyGpuCollect;
}

}  // namespace wlw::rendering

#endif  // WLW_PROFILE

#endif  // WLW_RENDERER_GL
