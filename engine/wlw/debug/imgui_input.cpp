#include "wlw/debug/imgui_input.h"

namespace wlw::debug {

namespace {

ImGuiKey Offset(ImGuiKey first, input::Key key, input::Key first_key) {
  return static_cast<ImGuiKey>(static_cast<int>(first) + (static_cast<int>(key) - static_cast<int>(first_key)));
}

}  // namespace

ImGuiKey ToImGuiKey(input::Key key) {
  using input::Key;
  if (key >= Key::A && key <= Key::Z) return Offset(ImGuiKey_A, key, Key::A);
  if (key >= Key::Digit0 && key <= Key::Digit9) return Offset(ImGuiKey_0, key, Key::Digit0);
  if (key >= Key::F1 && key <= Key::F12) return Offset(ImGuiKey_F1, key, Key::F1);
  if (key >= Key::Numpad0 && key <= Key::Numpad9) return Offset(ImGuiKey_Keypad0, key, Key::Numpad0);
  switch (key) {
    case Key::Grave: return ImGuiKey_GraveAccent;
    case Key::Minus: return ImGuiKey_Minus;
    case Key::Equal: return ImGuiKey_Equal;
    case Key::LeftBracket: return ImGuiKey_LeftBracket;
    case Key::RightBracket: return ImGuiKey_RightBracket;
    case Key::Backslash: return ImGuiKey_Backslash;
    case Key::Semicolon: return ImGuiKey_Semicolon;
    case Key::Apostrophe: return ImGuiKey_Apostrophe;
    case Key::Comma: return ImGuiKey_Comma;
    case Key::Period: return ImGuiKey_Period;
    case Key::Slash: return ImGuiKey_Slash;
    case Key::IntlBackslash: return ImGuiKey_Oem102;
    case Key::Space: return ImGuiKey_Space;
    case Key::Enter: return ImGuiKey_Enter;
    case Key::Escape: return ImGuiKey_Escape;
    case Key::Tab: return ImGuiKey_Tab;
    case Key::Backspace: return ImGuiKey_Backspace;
    case Key::Insert: return ImGuiKey_Insert;
    case Key::Delete: return ImGuiKey_Delete;
    case Key::Home: return ImGuiKey_Home;
    case Key::End: return ImGuiKey_End;
    case Key::PageUp: return ImGuiKey_PageUp;
    case Key::PageDown: return ImGuiKey_PageDown;
    case Key::Up: return ImGuiKey_UpArrow;
    case Key::Down: return ImGuiKey_DownArrow;
    case Key::Left: return ImGuiKey_LeftArrow;
    case Key::Right: return ImGuiKey_RightArrow;
    case Key::CapsLock: return ImGuiKey_CapsLock;
    case Key::ScrollLock: return ImGuiKey_ScrollLock;
    case Key::NumLock: return ImGuiKey_NumLock;
    case Key::PrintScreen: return ImGuiKey_PrintScreen;
    case Key::Pause: return ImGuiKey_Pause;
    case Key::Menu: return ImGuiKey_Menu;
    case Key::NumpadDecimal: return ImGuiKey_KeypadDecimal;
    case Key::NumpadDivide: return ImGuiKey_KeypadDivide;
    case Key::NumpadMultiply: return ImGuiKey_KeypadMultiply;
    case Key::NumpadSubtract: return ImGuiKey_KeypadSubtract;
    case Key::NumpadAdd: return ImGuiKey_KeypadAdd;
    case Key::NumpadEnter: return ImGuiKey_KeypadEnter;
    case Key::NumpadEqual: return ImGuiKey_KeypadEqual;
    case Key::LeftShift: return ImGuiKey_LeftShift;
    case Key::RightShift: return ImGuiKey_RightShift;
    case Key::LeftControl: return ImGuiKey_LeftCtrl;
    case Key::RightControl: return ImGuiKey_RightCtrl;
    case Key::LeftAlt: return ImGuiKey_LeftAlt;
    case Key::RightAlt: return ImGuiKey_RightAlt;
    case Key::LeftSuper: return ImGuiKey_LeftSuper;
    case Key::RightSuper: return ImGuiKey_RightSuper;
    case Key::Back: return ImGuiKey_AppBack;
    default: return ImGuiKey_None;
  }
}

ImGuiKey ToImGuiModifier(input::Key key) {
  using input::Key;
  switch (key) {
    case Key::LeftShift:
    case Key::RightShift: return ImGuiMod_Shift;
    case Key::LeftControl:
    case Key::RightControl: return ImGuiMod_Ctrl;
    case Key::LeftAlt:
    case Key::RightAlt: return ImGuiMod_Alt;
    case Key::LeftSuper:
    case Key::RightSuper: return ImGuiMod_Super;
    default: return ImGuiKey_None;
  }
}

scene::Cursor FromImGuiCursor(ImGuiMouseCursor cursor) {
  using scene::Cursor;
  switch (cursor) {
    case ImGuiMouseCursor_None: return Cursor::Hidden;
    case ImGuiMouseCursor_TextInput: return Cursor::TextInput;
    case ImGuiMouseCursor_ResizeAll: return Cursor::ResizeAll;
    case ImGuiMouseCursor_ResizeNS: return Cursor::ResizeNS;
    case ImGuiMouseCursor_ResizeEW: return Cursor::ResizeEW;
    case ImGuiMouseCursor_ResizeNESW: return Cursor::ResizeNESW;
    case ImGuiMouseCursor_ResizeNWSE: return Cursor::ResizeNWSE;
    case ImGuiMouseCursor_Hand: return Cursor::Hand;
    case ImGuiMouseCursor_NotAllowed: return Cursor::NotAllowed;
    default: return Cursor::Arrow;  // Arrow, Wait, Progress
  }
}

}  // namespace wlw::debug
