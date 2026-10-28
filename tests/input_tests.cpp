// Tests for wlw::input::Input: taps vs swipes, pointer blocking (used by the debug UI so a drag on one of its
// sliders doesn't also reach the game), keys, modifiers and shortcuts (D18), mouse buttons, wheel and movement,
// and what the debug UI tells Dear ImGui.
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "test_helpers.h"
#include "wlw/input/input.h"
#if WLW_DEBUG_UI
#include "wlw/debug/imgui_input.h"
#endif

TEST_SUITE_BEGIN("input");

using wlw::input::Input;
using wlw::input::Key;
using wlw::input::KeyEvent;
using wlw::input::KeyFromCharacter;
using wlw::input::KeyFromName;
using wlw::input::KeyLabels;
using wlw::input::KeyName;
using wlw::input::Modifiers;
using wlw::input::MouseButton;
using wlw::input::ShortcutName;
using wlw::input::Swipe;

TEST_CASE("tap and swipe") {
  Input in;
  in.Internal().SetSurfaceSize(1000, 2000);  // swipe threshold: 6% of the short side = 60 units
  in.Internal().BeginFrame();
  in.Internal().OnPointerDown(100, 100);
  in.Internal().OnPointerUp(110, 105);  // moved less than the threshold
  CHECK((in.WasPointerPressed() && in.WasPointerReleased() && in.WasTapped()));
  CHECK(in.Swipes().empty());

  in.Internal().BeginFrame();
  in.Internal().OnPointerDown(100, 100);
  in.Internal().OnPointerMove(100, 300);
  CHECK((in.Swipes().size() == 1 && in.Swipes()[0] == Swipe::Down));
  in.Internal().OnPointerUp(100, 300);
  CHECK(!in.WasTapped());  // a drag that swiped is not a tap
}

TEST_CASE("blocked pointer is invisible to the game") {
  Input in;
  in.Internal().SetSurfaceSize(1000, 2000);
  in.Internal().BeginFrame();
  in.Internal().OnPointerDown(100, 100);
  in.Internal().OnPointerMove(100, 300);
  in.SetPointerBlocked(true);
  CHECK((!in.IsPointerDown() && !in.WasPointerPressed() && in.Swipes().empty()));
  CHECK((in.RawPointerDown() && in.RawPointerPressed()));  // the owner still sees it
  CHECK(in.PointerPosition().y == 300.0f);                 // position stays readable

  in.Internal().OnPointerUp(100, 300);
  CHECK((!in.WasPointerReleased() && !in.WasTapped()));
  CHECK(in.RawPointerReleased());

  in.SetPointerBlocked(false);  // unblocked: the same frame's events are visible again
  CHECK((in.WasPointerPressed() && in.WasPointerReleased() && in.Swipes().size() == 1));
}

TEST_CASE("touch flag") {
  Input in;
  CHECK(!in.IsTouchScreen());
  in.Internal().SetTouchScreen(true);
  CHECK(in.IsTouchScreen());
}

TEST_CASE("every key: pressed and released inside one frame, repeats ignored") {
  Input in;
  for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
    const auto key = static_cast<Key>(i);
    CAPTURE(KeyName(key));
    in.Internal().BeginFrame();
    in.Internal().OnKey(key, true);
    in.Internal().OnKey(key, true);  // the OS's key repeat
    CHECK((in.IsKeyDown(key) && in.WasKeyPressed(key) && !in.WasKeyReleased(key)));
    in.Internal().OnKey(key, false);
    CHECK((!in.IsKeyDown(key) && in.WasKeyPressed(key) && in.WasKeyReleased(key)));
    CHECK(in.RawKeyEvents().size() == 2);  // down, up: the repeat isn't an event
    in.Internal().BeginFrame();
    CHECK((!in.WasKeyPressed(key) && !in.WasKeyReleased(key) && in.RawKeyEvents().empty()));
  }
  in.Internal().OnKey(Key::Unknown, true);
  CHECK(in.RawKeyEvents().empty());
}

TEST_CASE("key events keep the OS's order and name the key a release went down as") {
  Input in;
  in.Internal().BeginFrame();
  in.Internal().OnKey(Key::LeftControl, true);
  in.Internal().OnKey(Key::Y, true, Key::Z);  // QWERTZ: the key printed Z sits where US has Y
  in.Internal().OnKey(Key::Y, false);         // released without a layout key: still Z
  const std::vector<KeyEvent>& events = in.RawKeyEvents();
  REQUIRE(events.size() == 3);
  CHECK((events[0].key == Key::LeftControl && events[0].layout_key == Key::LeftControl && events[0].down));
  CHECK((events[1].key == Key::Y && events[1].layout_key == Key::Z && events[1].down));
  CHECK((events[2].key == Key::Y && events[2].layout_key == Key::Z && !events[2].down));
  CHECK((events[0].modifiers == Modifiers::Control && events[1].modifiers == Modifiers::Control));  // held after
}

TEST_CASE("modifiers and Primary") {
  Input in;
  CHECK(in.HeldModifiers() == Modifiers::None);
  in.Internal().OnKey(Key::RightShift, true);
  in.Internal().OnKey(Key::LeftAlt, true);
  CHECK(in.HeldModifiers() == (Modifiers::Shift | Modifiers::Alt));
  in.Internal().OnKey(Key::LeftControl, true);
  in.Internal().OnKey(Key::RightSuper, true);
  CHECK(in.HeldModifiers() == (Modifiers::Shift | Modifiers::Alt | Modifiers::Control | Modifiers::Super));
  in.SetKeyboardBlocked(true);
  CHECK(in.HeldModifiers() == Modifiers::None);
  CHECK(in.RawModifiers() != Modifiers::None);
#if defined(__APPLE__)
  CHECK(Modifiers::Primary == Modifiers::Super);  // Cmd
#else
  CHECK(Modifiers::Primary == Modifiers::Control);
#endif
}

TEST_CASE("shortcuts: exact modifiers, by the layout's key") {
  Input in;
  in.Internal().BeginFrame();
  in.Internal().OnKey(Key::LeftControl, true);
  in.Internal().OnKey(Key::Z, true);
  CHECK(in.WasShortcutPressed(Key::Z, Modifiers::Control));
  CHECK(!in.WasShortcutPressed(Key::Z));                                         // Ctrl is held
  CHECK(!in.WasShortcutPressed(Key::Z, Modifiers::Control | Modifiers::Shift));  // not Ctrl+Shift+Z
  CHECK(!in.WasShortcutPressed(Key::Y, Modifiers::Control));
  in.Internal().BeginFrame();
  CHECK(!in.WasShortcutPressed(Key::Z, Modifiers::Control));  // pressed last frame, only held now

  // The modifiers held when the key went down count, in the OS's order: Shift after the Z is still Ctrl+Z.
  in.Internal().OnKey(Key::Z, false);
  in.Internal().BeginFrame();
  in.Internal().OnKey(Key::Z, true);
  in.Internal().OnKey(Key::LeftShift, true);
  CHECK(in.WasShortcutPressed(Key::Z, Modifiers::Control));
  CHECK(!in.WasShortcutPressed(Key::Z, Modifiers::Control | Modifiers::Shift));
  in.Internal().OnKey(Key::Z, false);
  in.Internal().OnKey(Key::Z, true);  // again, Shift held this time
  CHECK(in.WasShortcutPressed(Key::Z, Modifiers::Control | Modifiers::Shift));
  in.SetKeyboardBlocked(true);  // a text field has the keyboard: its own Ctrl+Z
  CHECK(!in.WasShortcutPressed(Key::Z, Modifiers::Control | Modifiers::Shift));

  // A whole Ctrl+Z tapped inside one frame (a fast press, injected input): Ctrl is up by the end of the frame.
  Input tap;
  tap.Internal().BeginFrame();
  tap.Internal().OnKey(Key::RightControl, true);
  tap.Internal().OnKey(Key::Z, true);
  tap.Internal().OnKey(Key::Z, false);
  tap.Internal().OnKey(Key::RightControl, false);
  CHECK(tap.WasShortcutPressed(Key::Z, Modifiers::Control));
  CHECK(!tap.WasShortcutPressed(Key::Z));

  // AZERTY: the key printed A sits where US has Q. Shortcuts follow the label, controls the position.
  Input azerty;
  azerty.Internal().BeginFrame();
  azerty.Internal().OnKey(Key::LeftControl, true);
  azerty.Internal().OnKey(Key::Q, true, Key::A);
  CHECK(azerty.WasShortcutPressed(Key::A, Modifiers::Control));
  CHECK(!azerty.WasShortcutPressed(Key::Q, Modifiers::Control));
  CHECK((azerty.WasKeyPressed(Key::Q) && !azerty.WasKeyPressed(Key::A)));
}

TEST_CASE("key names round trip") {
  std::set<std::string> names;
  for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
    const auto key = static_cast<Key>(i);
    CAPTURE(i);
    CHECK(KeyFromName(KeyName(key)) == key);
    names.insert(KeyName(key));
  }
  CHECK(names.size() == static_cast<size_t>(Key::Count));  // all different
  CHECK(KeyFromName("pageup") == Key::PageUp);             // case ignored
  CHECK(KeyFromName("f5") == Key::F5);
  CHECK(KeyFromName("NoSuchKey") == Key::Unknown);
  CHECK(std::string(KeyName(Key::Digit7)) == "7");
}

TEST_CASE("layout characters to keys") {
  CHECK(KeyFromCharacter('a') == Key::A);
  CHECK(KeyFromCharacter('Z') == Key::Z);
  CHECK(KeyFromCharacter('0') == Key::Digit0);
  CHECK(KeyFromCharacter('9') == Key::Digit9);
  const std::pair<char, Key> punctuation[] = {
      {'`', Key::Grave},        {'-', Key::Minus},      {'=', Key::Equal},     {'[', Key::LeftBracket},
      {']', Key::RightBracket}, {'\\', Key::Backslash}, {';', Key::Semicolon}, {'\'', Key::Apostrophe},
      {',', Key::Comma},        {'.', Key::Period},     {'/', Key::Slash},
  };
  for (const auto& [character, key] : punctuation) {
    CAPTURE(character);
    CHECK(KeyFromCharacter(static_cast<unsigned char>(character)) == key);
  }
  CHECK(KeyFromCharacter(0x5D0) == Key::Unknown);  // a Hebrew letter: the key keeps its position
  CHECK(KeyFromCharacter('!') == Key::Unknown);
  CHECK(KeyFromCharacter(0) == Key::Unknown);
}

TEST_CASE("shortcut names per platform") {
  const Modifiers redo = Modifiers::Control | Modifiers::Shift;
  CHECK(ShortcutName(Key::Z, redo, KeyLabels::Pc) == "Ctrl+Shift+Z");
  CHECK(ShortcutName(Key::Z, Modifiers::Super | Modifiers::Shift, KeyLabels::Mac) == "Shift+Cmd+Z");
  CHECK(ShortcutName(Key::S, Modifiers::Control | Modifiers::Alt, KeyLabels::Mac) == "Ctrl+Option+S");
  CHECK(ShortcutName(Key::F5, Modifiers::None, KeyLabels::Pc) == "F5");
#if defined(__APPLE__)
  CHECK(ShortcutName(Key::S, Modifiers::Primary) == "Cmd+S");
#else
  CHECK(ShortcutName(Key::S, Modifiers::Primary) == "Ctrl+S");
#endif
}

TEST_CASE("mouse buttons: pressed and released inside one frame") {
  Input in;
  for (int i = 0; i < static_cast<int>(MouseButton::Count); ++i) {
    const auto button = static_cast<MouseButton>(i);
    CAPTURE(i);
    in.Internal().BeginFrame();
    in.Internal().OnMouseButton(button, true);
    in.Internal().OnMouseButton(button, true);
    CHECK((in.IsMouseButtonDown(button) && in.WasMouseButtonPressed(button)));
    in.Internal().OnMouseButton(button, false);
    CHECK((!in.IsMouseButtonDown(button) && in.WasMouseButtonPressed(button) && in.WasMouseButtonReleased(button)));
    in.Internal().BeginFrame();
    CHECK((!in.WasMouseButtonPressed(button) && !in.WasMouseButtonReleased(button)));
  }
  // Only the asked button.
  in.Internal().OnMouseButton(MouseButton::Right, true);
  CHECK((in.IsMouseButtonDown(MouseButton::Right) && !in.IsMouseButtonDown(MouseButton::Left)));
}

TEST_CASE("wheel: summed per frame, then reset") {
  Input in;
  in.Internal().BeginFrame();
  in.Internal().OnScroll(0.0f, 1.0f);
  in.Internal().OnScroll(0.25f, 1.0f);  // trackpads send fractions
  CHECK(in.ScrollDelta() == wlw::Vec2(0.25f, 2.0f));
  in.Internal().BeginFrame();
  CHECK(in.ScrollDelta() == wlw::Vec2(0.0f));
}

TEST_CASE("pointer delta") {
  Input in;
  in.Internal().BeginFrame();
  in.Internal().OnPointerMove(100, 100);  // the first position isn't movement
  CHECK(in.PointerDelta() == wlw::Vec2(0.0f));
  in.Internal().OnPointerMove(110, 95);
  in.Internal().OnPointerMove(130, 90);
  CHECK(in.PointerDelta() == wlw::Vec2(30.0f, -10.0f));
  in.Internal().BeginFrame();
  CHECK(in.PointerDelta() == wlw::Vec2(0.0f));
  in.Internal().ForgetPointerPosition();  // the cursor was locked: positions jump
  in.Internal().OnPointerMove(5000, -300);
  CHECK(in.PointerDelta() == wlw::Vec2(0.0f));

  // A finger only moves while touching: the next touch landing elsewhere is no movement.
  Input touch;
  touch.Internal().SetTouchScreen(true);
  touch.Internal().BeginFrame();
  touch.Internal().OnPointerDown(100, 100);
  touch.Internal().OnPointerMove(120, 100);
  touch.Internal().OnPointerUp(120, 100);
  CHECK(touch.PointerDelta() == wlw::Vec2(20.0f, 0.0f));
  touch.Internal().BeginFrame();
  touch.Internal().OnPointerDown(600, 900);
  touch.Internal().OnPointerMove(600, 910);
  CHECK(touch.PointerDelta() == wlw::Vec2(0.0f, 10.0f));
}

TEST_CASE("a blocked pointer hides buttons, wheel and movement") {
  Input in;
  in.Internal().BeginFrame();
  in.Internal().OnPointerMove(0, 0);
  in.Internal().OnPointerMove(10, 0);
  in.Internal().OnMouseButton(MouseButton::Right, true);
  in.Internal().OnScroll(0.0f, 1.0f);
  in.SetPointerBlocked(true);
  CHECK((!in.IsMouseButtonDown(MouseButton::Right) && !in.WasMouseButtonPressed(MouseButton::Right)));
  CHECK((in.ScrollDelta() == wlw::Vec2(0.0f) && in.PointerDelta() == wlw::Vec2(0.0f)));
  CHECK(
      (in.RawMouseButtonDown(MouseButton::Right) && in.RawScrollDelta().y == 1.0f && in.RawPointerDelta().x == 10.0f));
  in.SetPointerBlocked(false);
  CHECK((in.WasMouseButtonPressed(MouseButton::Right) && in.ScrollDelta().y == 1.0f));
}

TEST_CASE("release all (focus lost)") {
  Input in;
  in.Internal().BeginFrame();
  in.Internal().OnKey(Key::W, true);
  in.Internal().OnMouseButton(MouseButton::Middle, true);
  in.Internal().OnPointerDown(10, 10);
  in.Internal().BeginFrame();
  in.Internal().ReleaseAll();
  CHECK((!in.IsKeyDown(Key::W) && in.WasKeyReleased(Key::W)));
  CHECK((!in.IsMouseButtonDown(MouseButton::Middle) && in.WasMouseButtonReleased(MouseButton::Middle)));
  CHECK((!in.IsPointerDown() && in.WasPointerReleased() && !in.WasTapped()));  // not a tap
}

#if WLW_DEBUG_UI
TEST_CASE("every key has its own ImGui key") {
  std::set<int> seen;
  for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
    const auto key = static_cast<Key>(i);
    CAPTURE(KeyName(key));
    const ImGuiKey imgui = wlw::debug::ToImGuiKey(key);
    CHECK(imgui != ImGuiKey_None);
    CHECK(seen.insert(static_cast<int>(imgui)).second);
  }
  CHECK(wlw::debug::ToImGuiKey(Key::Unknown) == ImGuiKey_None);
  CHECK(wlw::debug::ToImGuiKey(Key::Z) == ImGuiKey_Z);  // the ranges line up
  CHECK(wlw::debug::ToImGuiKey(Key::Digit9) == ImGuiKey_9);
  CHECK(wlw::debug::ToImGuiKey(Key::F12) == ImGuiKey_F12);
  CHECK(wlw::debug::ToImGuiKey(Key::Numpad9) == ImGuiKey_Keypad9);
  CHECK(wlw::debug::ToImGuiModifier(Key::RightControl) == ImGuiMod_Ctrl);
  CHECK(wlw::debug::ToImGuiModifier(Key::A) == ImGuiKey_None);
}

TEST_CASE("ImGui cursor shapes") {
  using wlw::scene::Cursor;
  CHECK(wlw::debug::FromImGuiCursor(ImGuiMouseCursor_TextInput) == Cursor::TextInput);
  CHECK(wlw::debug::FromImGuiCursor(ImGuiMouseCursor_ResizeNWSE) == Cursor::ResizeNWSE);
  CHECK(wlw::debug::FromImGuiCursor(ImGuiMouseCursor_None) == Cursor::Hidden);
  CHECK(wlw::debug::FromImGuiCursor(ImGuiMouseCursor_Wait) == Cursor::Arrow);
}
#endif

TEST_SUITE_END();
