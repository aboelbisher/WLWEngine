#pragma once

#include <optional>

#include "imgui.h"
#include "wlw/input/input.h"
#include "wlw/scene/window.h"

namespace wlw::debug {

// What the debug UI tells Dear ImGui (its "platform backend" mappings; debug_ui.cpp feeds the events).

// Every key's own ImGui key (ImGuiKey_None for Key::Unknown). Fed with the layout's key (D18), like ImGui's GLFW
// backend, so Ctrl+Z is the key printed Z.
ImGuiKey ToImGuiKey(input::Key key);
// The ImGui modifier a modifier key holds (ImGuiMod_Ctrl for either Control...), ImGuiKey_None for other keys.
// ImGui swaps Ctrl and Cmd itself on macOS (io.ConfigMacOSXBehaviors).
ImGuiKey ToImGuiModifier(input::Key key);
// The window's cursor for ImGui's: None (or ImGui drawing its own) = Hidden; Wait / Progress = the arrow.
scene::Cursor FromImGuiCursor(ImGuiMouseCursor cursor);

}  // namespace wlw::debug
