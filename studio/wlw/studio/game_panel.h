#pragma once

#include <glm/glm.hpp>

#include "wlw/app/application.h"
#include "wlw/studio/devices.h"
#include "wlw/studio/game_input.h"
#include "wlw/studio/play_session.h"

namespace wlw::studio {

// The "Game" panel: the playing game's screen at the chosen device's size (Unity's Game view and Device Simulator,
// Unreal's PIE viewport), fitted into the panel; the device's safe area drawn over it on request. The game gets the
// Studio window's input through the panel (GameInputForwarder): the pointer over the picture, keys while the panel
// has focus; the game's cursor shape shows over the picture, and its cursor lock holds the Studio's cursor while the
// panel has focus (Shift+F1 lets go, as in Unreal; a click on the picture takes it again).
class GamePanel {
 public:
  explicit GamePanel(PlaySession& session) : session_(session) {}

  // The screen the game gets: the chosen device, turned, at the panel's size for Free.
  DeviceScreen Screen() const;

  // Before the game's frame: the device now, the cursor, the game's input for its frame.
  void BeforeGameFrame(app::Application& studio);
  // The panel (inside the Studio's ImGui frame, after the game's frame drew its picture).
  void Draw(app::Application& studio);
  // The device controls, for the toolbar: which device, turned, the safe area.
  void DrawDeviceControls();
  // Where the game's picture was drawn last frame, in the Studio window's pointer units (empty: not playing).
  const GameRect& PictureRect() const { return rect_; }

 private:
  void DrawSafeArea() const;
  void SetStudioCursorLocked(app::Application& studio, bool locked);

  PlaySession& session_;
  GameInputForwarder forwarder_;
  const app::Application* forwarded_game_ = nullptr;  // the session the forwarder's state is for
  int device_ = 0;                                    // DevicePresets() index
  bool turned_ = false;
  bool show_safe_area_ = false;
  glm::ivec2 free_size_{1280, 720};  // the panel's size in pixels (the Free device)
  GameRect rect_;                    // where the picture was drawn last frame
  bool hovered_ = false;             // last frame: the pointer was over the picture
  bool focused_ = false;             // last frame: the panel had focus
  bool lock_released_ = false;       // Shift+F1: the game's cursor lock let go until the next click on it
  bool studio_cursor_locked_ = false;
};

}  // namespace wlw::studio
