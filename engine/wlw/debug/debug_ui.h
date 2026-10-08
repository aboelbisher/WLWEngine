#pragma once

#include "wlw/scene/window.h"

namespace wlw::debug {

// In-game debug UI built on Dear ImGui (immediate mode: panels are rebuilt from your data every
// frame, so they can never show stale values). Only compiled with WLW_DEBUG_UI.
//
// ImGui needs two adapters ("backends"): a renderer - ImGui's own OpenGL3 backend, compiled
// against our GL (debug/imgui_gl_backend.cpp) - and a platform side, which is this class: it
// feeds ImGui from wlw::input::Input, so it works the same on desktop and Android.
//
// Per frame (driven by Application):
//   BeginFrame(dt)  feed input, start the ImGui frame, and *block the game's pointer* while ImGui
//                   wants it (dragging a slider must not also drag a game piece)
//   ...game update; Application::OnDebugUI() builds panels when the UI is open...
//   EndFrame()      finish; the window's overlay draws it on top of the scene
//
// Toggle: F1, or the small "Debug" button in the top-right corner (for touch screens).
class DebugUI {
 public:
  explicit DebugUI(scene::Window& window);  // the window's GL context must be current
  ~DebugUI();
  DebugUI(const DebugUI&) = delete;
  DebugUI& operator=(const DebugUI&) = delete;

  void BeginFrame(float dt);
  void EndFrame();

  bool IsOpen() const { return open_; }
  void SetOpen(bool open) { open_ = open; }

 private:
  void FeedInput(float dt);
  void DrawToggleButton();

  scene::Window& window_;
  bool open_ = false;
  bool imgui_pointer_down_ = false;  // the button state ImGui has been told about
  int overlay_id_ = -1;
};

}  // namespace wlw::debug
