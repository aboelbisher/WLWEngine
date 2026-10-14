#pragma once

// GPU zones for the profiler (core/profile.h), GL backend only: Tracy's OpenGL zones put timestamp queries
// around a stretch of GL calls, so the GPU's timeline shows under the CPU's in the viewer. Desktop GL 3.3 has
// timestamps in core; GLES needs EXT_disjoint_timer_query (Pixel 4 / Adreno has it, the emulator doesn't),
// whose functions libGLESv3 doesn't export: looked up at run time and named like the desktop ones here.
//
//   StartGpuProfiler()                         once the GL context is current (the renderer's Initialize)
//   WLW_PROFILE_GPU_ZONE("Main pass");         a GPU zone to the end of the scope (a string literal)
//   CollectGpuProfiler()                       once a frame, after the swap: finished queries -> Tracy
//
// The query context is made when a viewer first connects (its 64k queries cost nothing until then). Our own
// GpuTimer (gpu_timer.h, the engine panel's GPU ms) is separate and keeps working. Without WLW_PROFILE all of
// this is empty.

#include "wlw/core/profile.h"

namespace wlw::rendering {

#if WLW_PROFILE
void StartGpuProfiler();
void CollectGpuProfiler();
bool GpuProfilerActive();  // zones are recorded now (supported, context made, viewer connected)
#else
inline void StartGpuProfiler() {}
inline void CollectGpuProfiler() {}
inline bool GpuProfilerActive() { return false; }
#endif

}  // namespace wlw::rendering

#if WLW_PROFILE

#include "wlw/rendering/gl.h"

#if defined(WLW_GLES)
#include <cstdint>
namespace wlw::rendering::gles_timer_query {
// EXT_disjoint_timer_query functions GLES 3.0 lacks (null when unsupported).
extern void(GL_APIENTRY* query_counter)(GLuint id, GLenum target);
extern void(GL_APIENTRY* get_query_object_iv)(GLuint id, GLenum pname, GLint* params);
extern void(GL_APIENTRY* get_query_object_ui64v)(GLuint id, GLenum pname, GLuint64* params);
}  // namespace wlw::rendering::gles_timer_query
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28  // GL_TIMESTAMP_EXT
#endif
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS 0x8864  // GL_QUERY_COUNTER_BITS_EXT
#endif
#define glQueryCounter ::wlw::rendering::gles_timer_query::query_counter
#define glGetQueryObjectiv ::wlw::rendering::gles_timer_query::get_query_object_iv
#define glGetQueryObjectui64v ::wlw::rendering::gles_timer_query::get_query_object_ui64v
#endif

#include <tracy/TracyOpenGL.hpp>

#define WLW_PROFILE_GPU_ZONE(name) \
  TracyGpuNamedZone(WLW_PROFILE_CONCAT_(wlw_gpu_zone_, __LINE__), name, ::wlw::rendering::GpuProfilerActive())
// Named at run time (a null-terminated const char*, evaluated once), e.g. a post pass's name.
#define WLW_PROFILE_GPU_ZONE_DYNAMIC(name)                                      \
  const char* const WLW_PROFILE_CONCAT_(wlw_gpu_zone_name_, __LINE__) = (name); \
  TracyGpuZoneTransient(WLW_PROFILE_CONCAT_(wlw_gpu_zone_, __LINE__),           \
                        WLW_PROFILE_CONCAT_(wlw_gpu_zone_name_, __LINE__), ::wlw::rendering::GpuProfilerActive())

#else

#define WLW_PROFILE_GPU_ZONE(name) static_cast<void>(0)
#define WLW_PROFILE_GPU_ZONE_DYNAMIC(name) static_cast<void>(0)

#endif
