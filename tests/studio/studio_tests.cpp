// Tests for the Studio's logic without a window (studio/): the device presets and how a device's screen fits into
// the Game panel, and GameInputForwarder (the Studio window's input -> the game's, Unreal's PIE viewport).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <string_view>

#include "wlw/input/input.h"
#include "wlw/studio/devices.h"
#include "wlw/studio/game_input.h"

TEST_SUITE_BEGIN("studio");

using namespace wlw;
using input::Input;
using input::Key;
using input::MouseButton;
using studio::DevicePreset;
using studio::DeviceScreen;
using studio::GameInputForwarder;
using studio::GameInputFrame;
using studio::GameRect;

namespace {

const DevicePreset& Preset(std::string_view name) {
  for (const DevicePreset& preset : studio::DevicePresets()) {
    if (preset.name == name) return preset;
  }
  FAIL_CHECK("no preset ", name);
  return studio::DevicePresets()[0];
}

// A 100 x 200 picture at (10, 20) showing a 1000 x 2000 device: one pointer unit = 10 device pixels.
GameInputFrame Frame(bool hovered, bool focused = false, bool touch = false) {
  GameInputFrame frame;
  frame.rect = {.min = {10.0f, 20.0f}, .size = {100.0f, 200.0f}};
  frame.device = {1000, 2000};
  frame.hovered = hovered;
  frame.focused = focused;
  frame.touch = touch;
  return frame;
}

}  // namespace

TEST_CASE("studio: presets: Free first, phones upright with a safe area") {
  const auto presets = studio::DevicePresets();
  REQUIRE(presets.size() >= 4);
  CHECK(presets[0].size == glm::ivec2(0, 0));
  CHECK_FALSE(presets[0].touch);
  for (const DevicePreset& preset : presets) {
    if (!preset.touch) continue;
    CHECK(preset.size.y > preset.size.x);  // upright
    CHECK(preset.safe_area.top > 0.0f);
  }
}

TEST_CASE("studio: ScreenFor: Free takes the panel's size, turned swaps the size and moves the insets") {
  const DeviceScreen free = studio::ScreenFor(studio::DevicePresets()[0], true, {640, 360});
  CHECK(free.size == glm::ivec2(640, 360));  // never turned
  CHECK(studio::ScreenFor(studio::DevicePresets()[0], false, {0, 0}).size == glm::ivec2(1, 1));

  const DevicePreset& pixel = Preset("Pixel 4");
  const DeviceScreen upright = studio::ScreenFor(pixel, false, {640, 360});
  CHECK(upright.size == glm::ivec2(1080, 2280));
  CHECK(upright.touch);
  CHECK(upright.safe_area.top == pixel.safe_area.top);
  CHECK(upright.safe_area.bottom == pixel.safe_area.bottom);

  // A quarter turn left: the top (status bar) goes to the left side, the bottom to the right.
  const DeviceScreen turned = studio::ScreenFor(pixel, true, {640, 360});
  CHECK(turned.size == glm::ivec2(2280, 1080));
  CHECK(turned.safe_area.left == pixel.safe_area.top);
  CHECK(turned.safe_area.right == pixel.safe_area.bottom);
  CHECK(turned.safe_area.top == pixel.safe_area.right);
  CHECK(turned.safe_area.bottom == pixel.safe_area.left);
}

TEST_CASE("studio: FitGameRect: as large as fits, aspect kept, centred, zoom capped") {
  // A tall device in a wide panel: the height fills it.
  const GameRect tall = studio::FitGameRect({100.0f, 50.0f}, {800.0f, 400.0f}, {1000, 2000}, 8.0f);
  CHECK(tall.size.x == doctest::Approx(200.0f));
  CHECK(tall.size.y == doctest::Approx(400.0f));
  CHECK(tall.min.x == doctest::Approx(400.0f));  // 100 + (800 - 200) / 2
  CHECK(tall.min.y == doctest::Approx(50.0f));

  // A small device in a large panel: no more than max_zoom times its pixels.
  const GameRect small = studio::FitGameRect({0.0f, 0.0f}, {1000.0f, 1000.0f}, {10, 20}, 8.0f);
  CHECK(small.size.x == doctest::Approx(80.0f));
  CHECK(small.size.y == doctest::Approx(160.0f));
  CHECK(small.min.x == doctest::Approx(460.0f));

  CHECK(studio::FitGameRect({5.0f, 5.0f}, {0.0f, 100.0f}, {10, 10}, 8.0f).size == Vec2(0.0f));
  CHECK(studio::FitGameRect({5.0f, 5.0f}, {100.0f, 100.0f}, {0, 10}, 8.0f).size == Vec2(0.0f));
}

TEST_CASE("studio: ToDevicePixels and Contains") {
  const GameRect rect{.min = {10.0f, 20.0f}, .size = {100.0f, 200.0f}};
  CHECK(studio::ToDevicePixels(rect, {1000, 2000}, {10.0f, 20.0f}) == Vec2(0.0f, 0.0f));
  CHECK(studio::ToDevicePixels(rect, {1000, 2000}, {60.0f, 120.0f}) == Vec2(500.0f, 1000.0f));
  CHECK(studio::ToDevicePixels(rect, {1000, 2000}, {0.0f, 0.0f}) == Vec2(-100.0f, -200.0f));  // beyond the edge
  CHECK(studio::ToDevicePixels(GameRect{}, {1000, 2000}, {5.0f, 5.0f}) == Vec2(0.0f));
  CHECK(rect.Contains({10.0f, 20.0f}));
  CHECK_FALSE(rect.Contains({110.0f, 100.0f}));  // the far edges are outside
  CHECK_FALSE(rect.Contains({50.0f, 19.0f}));
}

TEST_CASE("studio: forwarder: a mouse click over the picture arrives in device pixels") {
  Input studio_input;
  Input game;
  GameInputForwarder forwarder;

  studio_input.Internal().OnPointerMove(60.0f, 120.0f);
  forwarder.Forward(studio_input, game, Frame(true));
  CHECK(game.PointerPosition() == Vec2(500.0f, 1000.0f));
  CHECK_FALSE(game.IsPointerDown());

  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnMouseButton(MouseButton::Left, true);
  studio_input.Internal().OnPointerDown(60.0f, 120.0f);
  forwarder.Forward(studio_input, game, Frame(true));
  CHECK(game.WasPointerPressed());
  CHECK(game.WasMouseButtonPressed(MouseButton::Left));
  CHECK(forwarder.Captured());

  // A drag goes on outside the picture (the game keeps the pointer), then the release arrives there.
  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnPointerMove(0.0f, 20.0f);
  forwarder.Forward(studio_input, game, Frame(false));
  CHECK(game.PointerPosition() == Vec2(-100.0f, 0.0f));
  CHECK(game.IsPointerDown());

  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnMouseButton(MouseButton::Left, false);
  studio_input.Internal().OnPointerUp(0.0f, 20.0f);
  forwarder.Forward(studio_input, game, Frame(false));
  CHECK(game.WasPointerReleased());
  CHECK_FALSE(game.IsMouseButtonDown(MouseButton::Left));
  CHECK_FALSE(forwarder.Captured());

  // Not over the picture and not dragging: the game hears nothing.
  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnPointerMove(5.0f, 5.0f);
  studio_input.Internal().OnMouseButton(MouseButton::Right, true);
  studio_input.Internal().OnScroll(0.0f, 1.0f);
  forwarder.Forward(studio_input, game, Frame(false));
  CHECK(game.PointerPosition() == Vec2(-100.0f, 0.0f));
  CHECK_FALSE(game.IsMouseButtonDown(MouseButton::Right));
  CHECK(game.ScrollDelta() == Vec2(0.0f));
}

TEST_CASE("studio: forwarder: other buttons and the wheel on a mouse, a click inside one frame") {
  Input studio_input;
  Input game;
  GameInputForwarder forwarder;
  studio_input.Internal().OnPointerMove(60.0f, 120.0f);
  studio_input.Internal().OnMouseButton(MouseButton::Right, true);
  studio_input.Internal().OnScroll(0.0f, -2.0f);
  forwarder.Forward(studio_input, game, Frame(true));
  CHECK(game.WasMouseButtonPressed(MouseButton::Right));
  CHECK_FALSE(game.IsPointerDown());  // only the left button is the pointer
  CHECK(game.ScrollDelta() == Vec2(0.0f, -2.0f));

  // Down and up between two frames: the game still sees the click.
  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnMouseButton(MouseButton::Middle, true);
  studio_input.Internal().OnMouseButton(MouseButton::Middle, false);
  forwarder.Forward(studio_input, game, Frame(true));
  CHECK(game.WasMouseButtonPressed(MouseButton::Middle));
  CHECK(game.WasMouseButtonReleased(MouseButton::Middle));
  CHECK_FALSE(game.IsMouseButtonDown(MouseButton::Middle));
}

TEST_CASE("studio: forwarder: a touch screen has one finger, no other buttons and no wheel") {
  Input studio_input;
  Input game;
  game.Internal().SetTouchScreen(true);
  GameInputForwarder forwarder;

  // Hovering moves nothing: a finger only moves while it touches.
  studio_input.Internal().OnPointerMove(60.0f, 120.0f);
  forwarder.Forward(studio_input, game, Frame(true, false, true));
  CHECK(game.PointerPosition() == Vec2(0.0f));

  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnMouseButton(MouseButton::Left, true);
  studio_input.Internal().OnPointerDown(60.0f, 120.0f);
  studio_input.Internal().OnMouseButton(MouseButton::Right, true);
  studio_input.Internal().OnScroll(0.0f, 1.0f);
  forwarder.Forward(studio_input, game, Frame(true, false, true));
  CHECK(game.WasPointerPressed());
  CHECK(game.PointerPosition() == Vec2(500.0f, 1000.0f));
  CHECK_FALSE(game.IsMouseButtonDown(MouseButton::Left));  // a finger, not a mouse button
  CHECK_FALSE(game.IsMouseButtonDown(MouseButton::Right));
  CHECK(game.ScrollDelta() == Vec2(0.0f));

  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnPointerMove(60.0f, 140.0f);
  forwarder.Forward(studio_input, game, Frame(true, false, true));
  CHECK(game.PointerPosition() == Vec2(500.0f, 1200.0f));  // dragged

  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnMouseButton(MouseButton::Left, false);
  studio_input.Internal().OnPointerUp(60.0f, 140.0f);
  forwarder.Forward(studio_input, game, Frame(true, false, true));
  CHECK(game.WasPointerReleased());
}

TEST_CASE("studio: forwarder: keys and text only with focus, and losing it lets go of everything") {
  Input studio_input;
  Input game;
  GameInputForwarder forwarder;

  studio_input.Internal().OnKey(Key::A, true, Key::A);
  studio_input.Internal().OnText('a');
  forwarder.Forward(studio_input, game, Frame(false, false));
  CHECK_FALSE(game.IsKeyDown(Key::A));
  CHECK(game.Text().empty());

  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  studio_input.Internal().OnKey(Key::W, true, Key::Z);  // a French keyboard: the W position types Z
  studio_input.Internal().OnText('z');
  studio_input.Internal().OnPointerMove(60.0f, 120.0f);
  studio_input.Internal().OnMouseButton(MouseButton::Left, true);
  studio_input.Internal().OnPointerDown(60.0f, 120.0f);
  forwarder.Forward(studio_input, game, Frame(true, true));
  CHECK(game.WasKeyPressed(Key::W));
  REQUIRE(game.RawKeyEvents().size() == 1);
  CHECK(game.RawKeyEvents()[0].layout_key == Key::Z);
  REQUIRE(game.Text().size() == 1);
  CHECK(game.Text()[0] == 'z');
  CHECK(game.IsPointerDown());

  // Focus goes to another panel: the key and the button go up in the game, as when a window loses focus.
  studio_input.Internal().BeginFrame();
  game.Internal().BeginFrame();
  forwarder.Forward(studio_input, game, Frame(false, false));
  CHECK_FALSE(game.IsKeyDown(Key::W));
  CHECK_FALSE(game.IsPointerDown());
  CHECK_FALSE(forwarder.Captured());
}

TEST_CASE("studio: forwarder: a locked cursor moves by raw motion, unbounded") {
  Input studio_input;
  Input game;
  GameInputForwarder forwarder;
  studio_input.Internal().OnPointerMove(60.0f, 120.0f);
  forwarder.Forward(studio_input, game, Frame(true));
  REQUIRE(game.PointerPosition() == Vec2(500.0f, 1000.0f));

  GameInputFrame locked = Frame(false, true);
  locked.cursor_locked = true;
  for (int i = 0; i < 3; ++i) {
    studio_input.Internal().BeginFrame();
    game.Internal().BeginFrame();
    studio_input.Internal().OnPointerMove(60.0f + 400.0f * static_cast<float>(i + 1), 120.0f);
    forwarder.Forward(studio_input, game, locked);
  }
  // From where it was when locked, by the Studio's motion (400 units a frame), far past the picture's edge.
  CHECK(game.PointerPosition() == Vec2(1700.0f, 1000.0f));
}

TEST_CASE("studio: forwarder: Reset forgets a held button") {
  Input studio_input;
  Input game;
  GameInputForwarder forwarder;
  studio_input.Internal().OnPointerMove(60.0f, 120.0f);
  studio_input.Internal().OnMouseButton(MouseButton::Left, true);
  studio_input.Internal().OnPointerDown(60.0f, 120.0f);
  forwarder.Forward(studio_input, game, Frame(true));
  CHECK(forwarder.Captured());
  forwarder.Reset();
  CHECK_FALSE(forwarder.Captured());
}

TEST_SUITE_END();
