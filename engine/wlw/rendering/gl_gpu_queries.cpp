#ifdef WLW_RENDERER_GL

#include "wlw/rendering/gl_gpu_queries.h"

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/gl.h"
#include "wlw/rendering/gl_resources.h"

namespace wlw::rendering {

namespace {

#if defined(WLW_GLES)
// EXT_disjoint_timer_query (most Mali, Adreno and PowerVR drivers): the GLES 3.0 query calls
// with its target, plus one function libGLESv3 doesn't export, looked up at run time.
constexpr GLenum kTimeElapsed = 0x88BF;  // GL_TIME_ELAPSED_EXT
constexpr GLenum kGpuDisjoint = 0x8FBB;  // GL_GPU_DISJOINT_EXT
using GetQueryObjectui64v = void(GL_APIENTRY*)(GLuint id, GLenum pname, GLuint64* params);
#else
constexpr GLenum kTimeElapsed = GL_TIME_ELAPSED;  // core in GL 3.3
#endif

class GLGpuQueries : public GpuQueries {
 public:
#if defined(WLW_GLES)
  explicit GLGpuQueries(GetQueryObjectui64v get_result) : get_result_(get_result) {}
#endif
  uint32_t Create() override {
    GLuint query = 0;
    glGenQueries(1, &query);
    return query;
  }
  void Destroy(uint32_t query) override {
    if (!platform::HasCurrentGLContext()) return;
    const GLuint id = query;
    glDeleteQueries(1, &id);
  }
  void Begin(uint32_t query) override { glBeginQuery(kTimeElapsed, query); }
  void End() override { glEndQuery(kTimeElapsed); }
  bool Available(uint32_t query) override {
    GLuint available = 0;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
    return available != 0;
  }
  uint64_t ElapsedNs(uint32_t query) override {
    GLuint64 ns = 0;
#if defined(WLW_GLES)
    get_result_(query, GL_QUERY_RESULT, &ns);
#else
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
#endif
    return ns;
  }
  bool Disjoint() override {
#if defined(WLW_GLES)
    GLint disjoint = 0;
    glGetIntegerv(kGpuDisjoint, &disjoint);
    return disjoint != 0;
#else
    return false;  // desktop GL has no such event
#endif
  }

 private:
#if defined(WLW_GLES)
  GetQueryObjectui64v get_result_;
#endif
};

}  // namespace

std::unique_ptr<GpuQueries> CreateGLGpuQueries() {
#if defined(WLW_GLES)
  if (!HasGLExtension("GL_EXT_disjoint_timer_query")) {
    WLW_LOG(LogRender, Info, "GPU timing: not supported on this GPU (no EXT_disjoint_timer_query)");
    return nullptr;
  }
  auto get_result = reinterpret_cast<GetQueryObjectui64v>(platform::GetGLProcAddress("glGetQueryObjectui64vEXT"));
  if (!get_result) {
    WLW_LOG(LogRender, Info, "GPU timing: not supported (glGetQueryObjectui64vEXT missing)");
    return nullptr;
  }
  return std::make_unique<GLGpuQueries>(get_result);
#else
  return std::make_unique<GLGpuQueries>();
#endif
}

}  // namespace wlw::rendering

#endif  // WLW_RENDERER_GL
