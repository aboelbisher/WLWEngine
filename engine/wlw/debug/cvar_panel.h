#pragma once

#include "imgui.h"
#include "wlw/app/system.h"

namespace wlw::debug {

// The "CVars" debug window (WLW_DEBUG_UI builds): every cvar (core/cvar.h), grouped by the prefix of its
// name (r., log., quality., a game's own), with a widget for its type, its help on hover and a reset
// button when it differs from its default. Changes go through the flags like the console's (source
// Console). Added by the Application after the engine panel.
class CVarPanel : public app::System {
 public:
  void OnDebugUI() override;

 private:
  ImGuiTextFilter filter_;
};

}  // namespace wlw::debug
