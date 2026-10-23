// Tests for wlw::input::Input: taps vs swipes, and pointer blocking (used by the debug UI so a
// drag on one of its sliders doesn't also reach the game).
#include "test_helpers.h"
#include "wlw/input/input.h"

TEST_SUITE_BEGIN("input");

using wlw::input::Input;
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

TEST_SUITE_END();
