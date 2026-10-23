#pragma once
#include <algorithm>
#include <memory>
#include <string>
#include <functional>
#include <utility>
#include <vector>

#include "wlw/core/color.h"
#include "wlw/core/connection.h"
#include "wlw/core/math.h"
#include "wlw/scene/viewport.h"
#include "wlw/input/input.h"

namespace wlw::scene {

struct WindowOptions {
  // false: never shown (desktop), just a GL context to draw offscreen viewports with, e.g. the
  // golden-image tests (tests/gpu). Phones always show their one full-screen surface.
  bool visible = true;
  // false: the screen surface has no depth buffer (saves its memory: ~17 MB on a 1440p phone). The
  // screen then always draws its 3D into its scene picture (its own depth) and copies it out, even
  // at render scale 1: for games whose screen is post-processed or render-scaled anyway.
  bool depth = true;
  // Desktop only: an OpenGL ES 3.0 context instead of OpenGL 3.3 core, so shaders compile as GLSL ES
  // like on phones (tests/gpu checks every shader that way). For compiling shaders, not for running
  // a game: the renderer's desktop paths assume desktop GL. Phones are always ES.
  bool gles = false;
};

// A drawable surface (the OS window / the phone's screen) + its input + what it shows: the
// main viewport (MainViewport(): a Scene seen through a camera, with the game's UI canvas over
// it) and any offscreen viewports, drawn into their textures before the screen (AddViewport).
// The platform layer implements the OS-specific part (platform/<os>/<os>_platform.cpp);
// nothing in this header depends on GLFW, EGL or any OS API.
class Window {
 public:
  virtual ~Window() = default;

  // ---- Platform-specific (implemented per OS) ----
  // Creates the OS window/surface and a GL context. `share_with`: share GPU resources with
  // another window's context (desktop multi-window; unsupported on mobile).
  virtual bool Initialize(Window* share_with = nullptr) = 0;
  virtual void MakeContextCurrent() = 0;
  virtual void ProcessEvents() = 0;      // poll OS events and feed GetInput()
  virtual void SwapBuffers() = 0;        // present the drawn frame
  virtual bool ShouldClose() const = 0;  // user closed the window/app (or RequestClose was called)
  virtual void RequestClose() = 0;
  virtual void SetTitle(const std::string& title) = 0;  // no-op where there is no title bar
  // Returns true once after the app comes back from the background (mobile), so the game
  // loop can restart its clock instead of seeing one huge frame.
  virtual bool ConsumeResumed() { return false; }
  // Returns true once after the app was sent to the background (mobile: call, app switch,
  // screen off), so the game can pause itself.
  virtual bool ConsumePaused() { return false; }
  // False while there's nothing to draw into (mobile: the OS took the surface away, e.g. in the
  // frame that reports the pause). Drawing is skipped then.
  virtual bool HasSurface() const { return true; }

  input::Input& GetInput() { return input_; }
  const input::Input& GetInput() const { return input_; }

  void SetIsActive(bool is_active) { is_active_ = is_active; }

  Vec2 GetSize() const {
    return size_;  // drawable size in pixels
  }

  // ---- What it shows ----
  // The main viewport, drawn to the screen: its scene (empty at first), camera (a default Camera3D
  // until SetCamera: at the origin looking along +Y) and the canvas the game's UI is drawn on
  // (Application::OnRenderUI).
  Viewport& MainViewport() { return *screen_; }
  const Viewport& MainViewport() const { return *screen_; }

  // Offscreen viewports, drawn every frame (as their update mode says) before the screen, in the
  // order added.
  void AddViewport(std::shared_ptr<Viewport> viewport) { viewports_.push_back(std::move(viewport)); }
  // Takes `viewport` out of the window and returns it (null if it wasn't added), like
  // Node3D::RemoveChild.
  std::shared_ptr<Viewport> RemoveViewport(const Viewport* viewport) {
    const auto it =
        std::find_if(viewports_.begin(), viewports_.end(),
                     [viewport](const std::shared_ptr<Viewport>& entry) { return entry.get() == viewport; });
    if (it == viewports_.end()) return nullptr;
    std::shared_ptr<Viewport> removed = std::move(*it);
    viewports_.erase(it);
    return removed;
  }
  const std::vector<std::shared_ptr<Viewport>>& Viewports() const { return viewports_; }

  // Whether the screen surface has a depth buffer (WindowOptions::depth).
  bool ScreenHasDepth() const { return screen_depth_; }

  // ---- GPU timing (rendering/gpu_timer.h) ----
  // Measures the GPU's time per viewport (Viewport::GetGpuStats) and per frame. On in debug
  // builds (the engine panel shows it), off in release builds unless turned on.
  void SetGpuTiming(bool on) { gpu_timing_ = on; }
  bool GpuTiming() const { return gpu_timing_; }
  // False until the renderer has started, then whether this GPU can measure (desktop GL: yes;
  // phones: EXT_disjoint_timer_query, which most have, and emulators often don't).
  bool GpuTimingSupported() const { return gpu_timing_supported_; }
  // The GPU's time for everything measured in a frame (the viewports' 3D, post passes and
  // canvases; not the debug UI), smoothed, in ms; -1 until measured.
  float GpuFrameMs() const { return gpu_frame_ms_; }

  // What fragment shaders' mediump floats are on this GPU (rendering/glsl_header.h): "16-bit",
  // "32-bit: no gain here", "ignored (desktop GL)"; set by the renderer when it starts.
  const std::string& FragmentPrecision() const { return fragment_precision_; }

  // ---- Framebuffer invalidation (GLES: glInvalidateFramebuffer) ----
  // Tells phone GPUs which pictures they needn't load before drawing or store after (depth after
  // the 3D, post textures about to be overwritten): memory bandwidth saved. On by default; the
  // switch is for measuring it (engine panel). Nothing on desktop GL (no such call in GL 3.3).
  void SetInvalidateFramebuffers(bool on) { invalidate_framebuffers_ = on; }
  bool InvalidateFramebuffers() const { return invalidate_framebuffers_; }

  // ---- Frame pacing (core/frame_pacing.h) ----
  // How fast frames are presented: frames per second, core::kTargetDisplayRate (the display's own
  // rate) or core::kTargetUncapped. Games set it with Application::SetTargetFps (cvar r.target_fps).
  // Android: Swappy (vsync + the limiter where it isn't available); desktop: a vsync interval, or
  // vsync off + a precise limiter. A hidden window is never paced. Game thread.
  virtual void SetTargetFps(int fps) { target_fps_ = fps; }
  int TargetFps() const { return target_fps_; }
  // Android (Swappy): lower the rate on its own (60 -> 30) while frames don't fit. Off by default:
  // Quality lowers the game's detail instead. Cvar r.pacing.auto_interval.
  virtual void SetAutoSwapInterval(bool on) { auto_swap_interval_ = on; }
  // The display's refresh rate in Hz (0: unknown).
  virtual float RefreshRate() const { return 0.0f; }
  // The rate frames are paced to (the target, or the display's rate when that is lower); 0 = no pacing.
  virtual float PacedFps() const { return 0.0f; }
  // How, for the engine panel and logs: "Swappy", "vsync", "vsync / 2", "limiter", "none".
  virtual std::string PacingMethod() const { return "none"; }

  class InternalAccess;
  // Engine-only parts (the platform's, the renderer's and the Application's): see InternalAccess.
  InternalAccess Internal();

 protected:
  Vec2 size_;  // drawable size in pixels (updated by the platform on resize)
  input::Input input_;

  bool is_active_ = true;

  std::unique_ptr<Viewport> screen_ = Viewport::CreateScreen();
  std::vector<std::shared_ptr<Viewport>> viewports_;
  core::CallbackList<> overlays_;
  bool screen_depth_ = true;
  bool gpu_timing_ = WLW_DEBUG_UI != 0;
  bool gpu_timing_supported_ = false;
  std::string fragment_precision_;
  float gpu_frame_ms_ = -1.0f;
  bool invalidate_framebuffers_ = true;
  int target_fps_ = 60;
  bool auto_swap_interval_ = false;

 public:
  static std::unique_ptr<Window> Create(const Vec2& size, const std::string& title, const WindowOptions& options = {});
};

// For the platform layer, the renderer and the Application, not games.
class Window::InternalAccess {
 public:
  explicit InternalAccess(Window& window) : window_(window) {}

  void SetScreenHasDepth(bool has) const { window_.screen_depth_ = has; }  // the platform
  void SetGpuTimingSupported(bool supported) const { window_.gpu_timing_supported_ = supported; }
  void SetGpuFrameMs(float ms) const { window_.gpu_frame_ms_ = ms; }
  void SetFragmentPrecision(std::string text) const { window_.fragment_precision_ = std::move(text); }

  // Drops the scenes and viewports (their GPU resources, if nothing else holds them): the engine
  // calls it at shutdown while the GL context still exists.
  void ReleaseScenes() const {
    window_.viewports_.clear();
    window_.screen_->SetScene(nullptr);
  }

  // Layers drawn on top of the screen (after its UI canvas), just before the frame is presented,
  // in the order added (e.g. the debug UI). Disconnect the Connection to remove one.
  core::Connection AddOverlay(std::function<void()> draw) const { return window_.overlays_.Add(std::move(draw)); }
  void DrawOverlays() const { window_.overlays_.Call(); }

 private:
  Window& window_;
};

inline Window::InternalAccess Window::Internal() { return InternalAccess(*this); }

}  // namespace wlw::scene
