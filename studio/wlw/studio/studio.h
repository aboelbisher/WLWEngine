#pragma once

#include <memory>
#include <set>
#include <string>
#include <utility>

#include "wlw/app/application.h"
#include "wlw/app/system.h"
#include "wlw/studio/game_panel.h"
#include "wlw/studio/play_session.h"

#if !WLW_DEBUG_UI
#error "The Studio is built on the debug UI's ImGui: it needs WLW_DEBUG_UI (CMake: WLW_BUILD_STUDIO)"
#endif

// wlw::studio: the Studio, the engine's editor (ROADMAP D12, D20). A desktop-only library on top of the engine (the
// runtime never includes it), all of its UI Dear ImGui (docking), portable C++ only: OS services come from the
// engine's platform layer. Two kinds of executable run it: wlw_studio (engine types only) and a game's <game>_studio
// (wlw_add_game(... STUDIO ...): the game's code built in, so Play runs it; WLW_MAIN calls RunStudio).
namespace wlw::studio {

// The Studio's main: its own Application (the OS window, the renderer, ImGui) with a StudioSystem. `game_name` and
// `make_game` are the game's (null: wlw_studio, nothing to play). Returns the process exit code.
int RunStudio(int argc, char** argv, const char* game_name, MakeGameFn make_game);

// The Studio's AppConfig: its own save folder (<game>_studio: the panel layout), its window, no audio of its own
// (each session has the game's).
app::AppConfig StudioConfig(const char* game_name);

// The Studio itself, a System on its Application (like an editor's main frame): the dockspace over the window, the
// menu bar, the toolbar (Play / Pause / Step / Stop, the device), the status bar, the Game panel; the engine's
// debug panels (Engine, CVars, Console) dock next to it, and so do the game's own while it plays. Play / Stop run
// one PlaySession; its frame runs inside this system's OnDebugUI, so the game's panels go into the same frame.
// Escape never closes the Studio (it goes to the game when the Game panel has focus). The panel layout is saved in
// the Studio's save folder (studio_layout.ini); Window > Reset layout brings back the default one.
class StudioSystem : public app::System {
 public:
  StudioSystem(std::string game_name, MakeGameFn make_game) : game_name_(std::move(game_name)), make_game_(make_game) {}

  void OnStart() override;
  void OnDebugUI() override;
  bool OnBack() override { return true; }  // Escape: nothing in the Studio closes on it
  void OnShutdown() override;

  // What the toolbar's buttons and shortcuts do (Unity's: Primary+P, Primary+Shift+P, Primary+Alt+P); they take
  // effect at the start of the next game frame.
  void Play() { pending_ = Pending::Play; }
  void Stop() { pending_ = Pending::Stop; }
  void TogglePause() { pending_ = Pending::TogglePause; }
  void Step() { pending_ = Pending::Step; }  // pauses first if playing

  PlaySession& Session() { return *session_; }
  GamePanel& Game() { return *game_panel_; }

 private:
  enum class Pending { None, Play, Stop, TogglePause, Step };

  void DrawFrameStart();  // first in every ImGui frame: menus, bars, the dockspace (before the windows it holds)
  void DrawMenuBar();
  void DrawToolbar();
  void DrawStatusBar();
  void HandleShortcuts();
  void ApplyPending();
  void BuildDefaultLayout(unsigned int dockspace);
  void DockNewWindows();  // a panel the layout has never seen (the game's own) docks where the Engine panel is

  std::string game_name_;
  MakeGameFn make_game_;
  std::unique_ptr<PlaySession> session_;
  std::unique_ptr<GamePanel> game_panel_;
  Pending pending_ = Pending::None;
  bool reset_layout_ = false;
  std::string layout_file_;  // ImGui keeps a pointer to it
  std::set<std::string> docked_;  // panels DockNewWindows placed this run
};

}  // namespace wlw::studio
