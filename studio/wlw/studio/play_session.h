#pragma once

#include <memory>
#include <optional>

#include "wlw/app/application.h"
#include "wlw/scene/offscreen_window.h"
#include "wlw/studio/devices.h"

namespace wlw::studio {

// The game's function that builds its app (its MakeApp(), the one WLW_MAIN takes).
using MakeGameFn = std::unique_ptr<app::Application> (*)();

// One Play of the game inside the Studio, like Unreal's Play In Editor session: Play builds a fresh app with the
// game's MakeApp() and starts it on an OffscreenWindow at the simulated device's size, hosted on the Studio's app
// (Application::Internal().InitHosted: the Studio's renderer, GL context and ImGui); Stop shuts it down and
// destroys it, so nothing of the game runs or stays between sessions. The game ends a session itself by quitting
// (Quit, or Escape / Back with nothing left to close). Game thread.
class PlaySession {
 public:
  PlaySession(app::Application& studio, MakeGameFn make_game) : studio_(studio), make_game_(make_game) {}
  ~PlaySession() { Stop(); }
  PlaySession(const PlaySession&) = delete;
  PlaySession& operator=(const PlaySession&) = delete;

  // Whether this Studio has a game to play (wlw_studio has none: engine types only).
  bool CanPlay() const { return make_game_ != nullptr; }
  bool Playing() const { return game_ != nullptr; }

  // Starts a fresh session on `screen` (stops a running one first). False when the game couldn't start.
  bool Play(const DeviceScreen& screen);
  void Stop();
  void SetPaused(bool paused);
  bool Paused() const;
  void Step();  // paused: the next frame moves one fixed step

  // The game's next frame (draws its screen into the window's texture; its debug panels into the Studio's ImGui
  // frame). When the game quits, the session ends here and its exit code is kept.
  void Frame();
  // The device the game runs on now (size, safe area, touch): applied between frames.
  void SetScreen(const DeviceScreen& screen);

  // The game's window and app while playing, else null.
  scene::OffscreenWindow* Window() const { return window_.get(); }
  app::Application* Game() const { return game_.get(); }
  // How the last session ended by itself: the game's exit code (nullopt: still playing, stopped, or never ran).
  std::optional<int> LastExitCode() const { return last_exit_code_; }

 private:
  app::Application& studio_;
  MakeGameFn make_game_;
  std::shared_ptr<scene::OffscreenWindow> window_;
  std::unique_ptr<app::Application> game_;
  std::optional<int> last_exit_code_;
};

}  // namespace wlw::studio
