#pragma once

#include <array>

#include "wlw/debug/console_window.h"
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
// Toggle: F1, or the small "Debug" button in the top-right corner (for touch screens). The console: the `
// key (opens the debug UI too) or the "Console" button next to it. While an ImGui text field has the
// keyboard (typing in the console), the game's keyboard queries are blocked.
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
  // Closes the panel and hides the toggle button for `seconds` (clean screenshots and screen
  // recordings); it comes back by itself, so it can't get lost.
  void HideFor(float seconds) {
    open_ = false;
    hidden_for_ = seconds;
  }

 private:
  void FeedInput(float dt);
  void FeedKeyboard();
  void UpdateCursor();
  void DrawToggleButton();

  scene::Window& window_;
  bool open_ = false;
  float hidden_for_ = 0.0f;                   // seconds left with the toggle button hidden
  std::array<bool, 5> imgui_buttons_down_{};  // the button states ImGui has been told about
  bool imgui_focused_ = true;
  core::Connection overlay_;
  ConsoleWindow console_;
};

}  // namespace wlw::debug
