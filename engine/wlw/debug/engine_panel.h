#pragma once

#include "wlw/app/system.h"
#include "wlw/core/math.h"
#include "wlw/scene/window.h"

namespace wlw::debug {

// The engine's own debug window (WLW_DEBUG_UI builds): frame time, draw calls, viewports,
// framebuffer and pointer sizes, input (keys, buttons, wheel; cursor shapes and a timed lock to try by hand), the
// ImGui demo. An ordinary System that the Application adds first, so its panel comes before the game's.
class EnginePanel : public app::System {
 public:
  void OnUpdate(float dt) override;
  void OnDebugUI() override;

 private:
  void InputSection(scene::Window& window);

  bool show_imgui_demo_ = false;
  float lock_seconds_left_ = 0.0f;  // the panel's timed cursor lock (ended in OnUpdate, even with the UI closed)
  Vec2 locked_motion_{0.0f};
  Vec2 last_scroll_{0.0f};
};

}  // namespace wlw::debug
