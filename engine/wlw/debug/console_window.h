#pragma once

#include "imgui.h"
#include "wlw/core/console.h"

namespace wlw::debug {

// The in-game console (WLW_DEBUG_UI builds), drawn by DebugUI: the log (from its recent history on, live),
// coloured by level and filterable, and an input line for commands and cvars (core/console.h) with history
// (Up / Down) and Tab completion. Opened with the ` key or the "Console" button.
//
// Phones have no keyboard for it yet (that needs GameTextInput, roadmap D9): there it shows the log, and
// cvars are changed in the CVars panel or with adb (--es wlw "name=value;...").
class ConsoleWindow {
 public:
  bool open = false;

  // `keyboard`: a physical keyboard can type into it (desktop).
  void Draw(bool keyboard);
  // Puts the cursor in the input line on the next Draw (after opening it).
  void FocusInput() { focus_input_ = true; }

 private:
  static int InputCallback(ImGuiInputTextCallbackData* data);

  ConsoleModel model_;
  char input_[512] = {};
  ImGuiTextFilter filter_;
  int min_level_ = 0;  // LogLevel shown and above
  bool follow_ = true;  // keep the newest line in view
  bool focus_input_ = false;
};

}  // namespace wlw::debug
