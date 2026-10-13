#pragma once

#include <memory>

#include "wlw/rendering/gpu_timer.h"

namespace wlw::rendering {

// GL timer queries for a GpuTimer: core on desktop GL 3.3; EXT_disjoint_timer_query on GLES 3.0.
// Null (logged) if this GPU has neither. Needs the GL context.
std::unique_ptr<GpuQueries> CreateGLGpuQueries();

}  // namespace wlw::rendering
