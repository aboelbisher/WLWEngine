#pragma once

#include <array>
#include <functional>
#include <optional>
#include <utility>

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
//
// Text and widgets follow the window's ContentScale (desktop display scaling) or, on phones, the screen's size.
// The Studio (studio/) runs it in Studio mode: always open, docking on, no toggle button or F1.
class DebugUI {
 public:
  explicit DebugUI(scene::Window& window);  // the window's GL context must be current
  ~DebugUI();
  DebugUI(const DebugUI&) = delete;
  DebugUI& operator=(const DebugUI&) = delete;

  void BeginFrame(float dt);
  void EndFrame();
  // Between BeginFrame and EndFrame: ImGui windows may be built now (a Studio's hosted game session draws its
  // OnDebugUI panels only then).
  bool InFrame() const { return in_frame_; }

  bool IsOpen() const { return open_; }
  void SetOpen(bool open) { open_ = open || studio_mode_; }
  // Closes the panel and hides the toggle button for `seconds` (clean screenshots and screen
  // recordings); it comes back by itself, so it can't get lost.
  void HideFor(float seconds) {
    if (studio_mode_) return;
    open_ = false;
    hidden_for_ = seconds;
  }

  // The Studio's: always open, panels dock (ImGui's docking), no toggle button and no F1; the console is one of
  // the panels (the ` key still opens it). Set before the first frame.
  void SetStudioMode(bool on);
  bool StudioMode() const { return studio_mode_; }
  ConsoleWindow& Console() { return console_; }
  // The cursor shape for the next frame while ImGui has the mouse, instead of ImGui's own (the Studio's Game
  // panel shows the game's cursor over the game). Asked again every frame it applies.
  void RequestCursor(scene::Cursor cursor) { requested_cursor_ = cursor; }
  // Drawn right after each ImGui frame starts, before any panel: the Studio's dockspace, menu and bars (a
  // dockspace must come before the windows it holds).
  void SetFrameStart(std::function<void()> draw) { frame_start_ = std::move(draw); }

 private:
  void FeedInput(float dt);
  void FeedKeyboard();
  void UpdateCursor();
  void DrawToggleButton();
  void ApplyScale();  // the style's sizes and the text for the window's scale, when it changes

  scene::Window& window_;
  bool open_ = false;
  bool in_frame_ = false;
  bool studio_mode_ = false;
  float scale_ = 0.0f;  // the scale the style was made for (0: none yet)
  std::optional<scene::Cursor> requested_cursor_;
  std::function<void()> frame_start_;
  float hidden_for_ = 0.0f;                   // seconds left with the toggle button hidden
  std::array<bool, 5> imgui_buttons_down_{};  // the button states ImGui has been told about
  bool imgui_focused_ = true;
  core::Connection overlay_;
  ConsoleWindow console_;
};

}  // namespace wlw::debug
