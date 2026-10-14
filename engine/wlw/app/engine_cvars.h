#pragma once

#include "wlw/core/cvar.h"

// The engine's own cvars (core/cvar.h). The Application applies them to the window, the log and the
// quality levels, and keeps them in step when they change (console, debug panels, config files).
namespace wlw::app {

// Log filters, like SetLogFilter: "Render=Verbose,Audio=Warning,*=Info"; "" = every category at Info.
inline CVar<std::string> cv_log_filter{"log.filter", "", "Log levels per category: Render=Verbose,*=Warning"};

// GPU time per viewport and frame (engine panel); costs a few queries per frame.
inline CVar<bool> cv_gpu_timing{"r.gpu_timing", WLW_DEBUG_UI != 0, "Measure the GPU's time per viewport and frame"};

// glInvalidateFramebuffer on GLES (saves phone memory bandwidth); off only to measure it.
inline CVar<bool> cv_invalidate_framebuffers{"r.invalidate_framebuffers", true,
                                             "Tell phone GPUs which pictures needn't be loaded or stored"};

// A fixed quality level (app/quality.h), or -1 to let the engine choose.
inline CVar<int> cv_quality_lock{"quality.lock", -1, "Fixed quality level, -1 = automatic", CVarFlags::None,
                                 CVar<int>::Range{-1, 15}};

}  // namespace wlw::app
