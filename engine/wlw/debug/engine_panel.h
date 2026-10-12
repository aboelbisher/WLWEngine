#pragma once

#include "wlw/app/system.h"

namespace wlw::debug {

// The engine's own debug window (WLW_DEBUG_UI builds): frame time, draw calls, viewports,
// framebuffer and pointer sizes, the ImGui demo. An ordinary System that the Application adds
// first, so its panel comes before the game's.
class EnginePanel : public app::System {
 public:
  void OnDebugUI() override;

 private:
  bool show_imgui_demo_ = false;
};

}  // namespace wlw::debug
