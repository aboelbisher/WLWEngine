// Tests for wlw::input::Input: taps vs swipes, and pointer blocking (used by the debug UI so a
// drag on one of its sliders doesn't also reach the game).
#include "check.h"
#include "wlw/input/input.h"

using wlw::input::Input;
using wlw::input::Swipe;

static void TestTapAndSwipe() {
  Input in;
  in.SetSurfaceSize(1000, 2000);  // swipe threshold: 6% of the short side = 60 units
  in.BeginFrame();
  in.OnPointerDown(100, 100);
  in.OnPointerUp(110, 105);  // moved less than the threshold
  CHECK(in.WasPointerPressed() && in.WasPointerReleased() && in.WasTapped());
  CHECK(in.Swipes().empty());

  in.BeginFrame();
  in.OnPointerDown(100, 100);
  in.OnPointerMove(100, 300);
  CHECK(in.Swipes().size() == 1 && in.Swipes()[0] == Swipe::Down);
  in.OnPointerUp(100, 300);
  CHECK(!in.WasTapped());  // a drag that swiped is not a tap
}

static void TestBlockedPointerIsInvisibleToTheGame() {
  Input in;
  in.SetSurfaceSize(1000, 2000);
  in.BeginFrame();
  in.OnPointerDown(100, 100);
  in.OnPointerMove(100, 300);
  in.SetPointerBlocked(true);
  CHECK(!in.IsPointerDown() && !in.WasPointerPressed() && in.Swipes().empty());
  CHECK(in.RawPointerDown() && in.RawPointerPressed());  // the owner still sees it
  CHECK(in.PointerPosition().y == 300.0f);                // position stays readable

  in.OnPointerUp(100, 300);
  CHECK(!in.WasPointerReleased() && !in.WasTapped());
  CHECK(in.RawPointerReleased());

  in.SetPointerBlocked(false);  // unblocked: the same frame's events are visible again
  CHECK(in.WasPointerPressed() && in.WasPointerReleased() && in.Swipes().size() == 1);
}

static void TestTouchFlag() {
  Input in;
  CHECK(!in.IsTouchScreen());
  in.SetTouchScreen(true);
  CHECK(in.IsTouchScreen());
}

int main() {
  TestTapAndSwipe();
  TestBlockedPointerIsInvisibleToTheGame();
  TestTouchFlag();
  return TestResult("input");
}
