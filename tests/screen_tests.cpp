// Tests for game states (app::ScreenStack) and the immediate-mode ui::Canvas::Button.
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "wlw/app/screen.h"
#include "wlw/ui/canvas.h"

using wlw::app::Screen;
using wlw::app::ScreenStack;
using Transition = ScreenStack::Transition;

namespace {

std::vector<std::string> g_log;

// Records every hook call as "<name>:<hook>".
class Recorder : public Screen {
 public:
  Recorder(std::string name, bool overlay = false, bool handles_back = false)
      : name_(std::move(name)), overlay_(overlay), handles_back_(handles_back) {}
  void OnEnter() override { Log("enter"); }
  void OnExit() override { Log("exit"); }
  void OnCovered() override { Log("covered"); }
  void OnUncovered() override { Log("uncovered"); }
  void Update(float) override {
    Log("update");
    if (pop_on_update) Stack().Pop();
  }
  void Render(float) override { Log("render"); }
  bool OnBack() override {
    Log("back");
    return handles_back_;
  }
  void OnAppPaused() override { Log("paused"); }
  bool IsOverlay() const override { return overlay_; }
  bool pop_on_update = false;

 private:
  void Log(const char* what) { g_log.push_back(name_ + ":" + what); }
  std::string name_;
  bool overlay_;
  bool handles_back_;
};

bool LogIs(const std::vector<std::string>& expected) {
  const bool same = g_log == expected;
  if (!same) {
    std::printf("  log was:");
    for (const auto& entry : g_log) std::printf(" %s", entry.c_str());
    std::printf("\n");
  }
  g_log.clear();
  return same;
}

}  // namespace

static void TestPushIsDeferredAndCoversTheScreenBelow() {
  g_log.clear();
  ScreenStack stack;
  stack.Push(std::make_unique<Recorder>("menu"));
  CHECK(stack.Empty() && g_log.empty());  // nothing happens until the frame ends
  stack.EndFrame(0.016f);
  CHECK(stack.Size() == 1 && LogIs({"menu:enter"}));

  stack.Push(std::make_unique<Recorder>("play"));
  stack.EndFrame(0.016f);
  CHECK(LogIs({"menu:covered", "play:enter"}));
  stack.Update(0.016f);
  stack.Render(0.0f);
  CHECK(LogIs({"play:update", "play:render"}));  // only the top updates; menu is hidden (opaque top)
}

static void TestOverlayDrawsTheScreenBelowAndPopUncovers() {
  g_log.clear();
  ScreenStack stack;
  stack.Push(std::make_unique<Recorder>("play"));
  stack.Push(std::make_unique<Recorder>("pause", /*overlay=*/true));
  stack.EndFrame(0.016f);
  CHECK(LogIs({"play:enter", "play:covered", "pause:enter"}));
  stack.Update(0.016f);
  stack.Render(0.0f);
  CHECK(LogIs({"pause:update", "play:render", "pause:render"}));  // frozen, but drawn, bottom first

  stack.Pop();
  stack.EndFrame(0.016f);
  CHECK(LogIs({"pause:exit", "play:uncovered"}));
  CHECK(stack.Size() == 1);
}

static void TestReplaceAndReset() {
  g_log.clear();
  ScreenStack stack;
  stack.Push(std::make_unique<Recorder>("a"));
  stack.Push(std::make_unique<Recorder>("b"));
  stack.EndFrame(0.016f);
  g_log.clear();
  stack.Replace(std::make_unique<Recorder>("c"));
  stack.EndFrame(0.016f);
  CHECK(LogIs({"b:exit", "c:enter"}));  // 'a' stays covered: no events for it
  stack.Reset(std::make_unique<Recorder>("menu"));
  stack.EndFrame(0.016f);
  CHECK(LogIs({"c:exit", "a:exit", "menu:enter"}));  // top down
  CHECK(stack.Size() == 1);
}

static void TestScreenCanPopItselfDuringUpdate() {
  g_log.clear();
  ScreenStack stack;
  stack.Push(std::make_unique<Recorder>("play"));
  auto dialog = std::make_unique<Recorder>("dialog", true);
  dialog->pop_on_update = true;
  stack.Push(std::move(dialog));
  stack.EndFrame(0.016f);
  g_log.clear();
  stack.Update(0.016f);  // pops itself: must still be alive for the rest of this frame
  stack.Render(0.0f);
  CHECK(stack.Size() == 2);
  stack.EndFrame(0.016f);
  CHECK(LogIs({"dialog:update", "play:render", "dialog:render", "dialog:exit", "play:uncovered"}));
}

static void TestBackGoesTopDown() {
  g_log.clear();
  ScreenStack stack;
  stack.Push(std::make_unique<Recorder>("play", false, /*handles_back=*/true));
  stack.Push(std::make_unique<Recorder>("toast", true, false));
  stack.EndFrame(0.016f);
  g_log.clear();
  CHECK(stack.Back());  // the toast doesn't handle it, the game below does
  CHECK(LogIs({"toast:back", "play:back"}));
  stack.AppPaused();
  CHECK(LogIs({"toast:paused"}));  // top only

  ScreenStack lone;
  lone.Push(std::make_unique<Recorder>("menu"));
  lone.EndFrame(0.016f);
  g_log.clear();
  CHECK(!lone.Back());  // nobody handled it: the app leaves
}

static void TestFadeTransition() {
  g_log.clear();
  ScreenStack stack;
  stack.fade_time = 0.2f;
  stack.Push(std::make_unique<Recorder>("menu"));
  stack.EndFrame(0.016f);
  g_log.clear();

  stack.Replace(std::make_unique<Recorder>("play"), Transition::Fade);
  stack.EndFrame(0.0f);  // starts fading out; nothing changed yet
  CHECK(stack.Transitioning() && stack.Top() && g_log.empty());
  stack.EndFrame(0.1f);
  CHECK(Near(stack.FadeAmount(), 0.5f, 1e-3f));
  stack.Update(0.016f);
  CHECK(g_log.empty());  // no updates (and no input) during a transition
  CHECK(stack.Back());   // Back is swallowed too
  stack.EndFrame(0.11f);  // fully black: the switch happens now
  CHECK(LogIs({"menu:exit", "play:enter"}));
  CHECK(Near(stack.FadeAmount(), 1.0f, 1e-3f));
  stack.EndFrame(0.1f);
  CHECK(Near(stack.FadeAmount(), 0.5f, 1e-3f));  // fading back in
  stack.EndFrame(0.11f);
  CHECK(!stack.Transitioning() && stack.FadeAmount() == 0.0f);
}

static void TestClearExitsEveryone() {
  g_log.clear();
  {
    ScreenStack stack;
    stack.Push(std::make_unique<Recorder>("a"));
    stack.Push(std::make_unique<Recorder>("b"));
    stack.EndFrame(0.016f);
    g_log.clear();
    stack.Clear();
    CHECK(LogIs({"b:exit", "a:exit"}) && stack.Empty());
  }
  CHECK(g_log.empty());  // nothing left for the destructor
}

// ---- Buttons ----

static void Frame(wlw::ui::Canvas& canvas, glm::vec2 at, bool down, bool pressed, bool released) {
  canvas.BeginFrame({400.0f, 800.0f}, {}, {400.0f, 800.0f});  // 1 unit = 1 pixel
  wlw::ui::PointerState pointer;
  pointer.position = at;
  pointer.down = down;
  pointer.pressed = pressed;
  pointer.released = released;
  canvas.SetPointer(pointer);
}

static void TestButtonClicks(const wlw::ui::Font& font) {
  wlw::ui::Canvas canvas;
  const wlw::ui::Rect play{{100.0f, 100.0f}, {300.0f, 160.0f}};
  const wlw::ui::Rect quit{{100.0f, 200.0f}, {300.0f, 260.0f}};
  const glm::vec2 on_play(200.0f, 130.0f), on_quit(200.0f, 230.0f), nowhere(10.0f, 10.0f);

  // Press and release on it (over two frames): one click, on release.
  Frame(canvas, on_play, true, true, false);
  CHECK(!canvas.Button(font, "PLAY", play));
  Frame(canvas, on_play, false, false, true);
  CHECK(canvas.Button(font, "PLAY", play));
  CHECK(!canvas.Button(font, "QUIT", quit));

  // A quick tap inside one frame still counts.
  Frame(canvas, on_play, false, true, true);
  CHECK(canvas.Button(font, "PLAY", play));

  // Press on it, slide off, release: cancelled. Press elsewhere, release on it: nothing.
  Frame(canvas, on_play, true, true, false);
  canvas.Button(font, "PLAY", play);
  Frame(canvas, nowhere, false, false, true);
  CHECK(!canvas.Button(font, "PLAY", play));
  Frame(canvas, on_quit, true, true, false);
  canvas.Button(font, "PLAY", play);
  Frame(canvas, on_play, false, false, true);
  CHECK(!canvas.Button(font, "PLAY", play));

  // Not interactive (a screen that isn't on top): drawn, but never clicked.
  Frame(canvas, on_play, false, true, true);
  canvas.SetInteractive(false);
  const size_t quads_before = canvas.QuadCount();
  CHECK(!canvas.Button(font, "PLAY", play));
  CHECK(canvas.QuadCount() > quads_before);
}

int main() {
  TestPushIsDeferredAndCoversTheScreenBelow();
  TestOverlayDrawsTheScreenBelowAndPopUncovers();
  TestReplaceAndReset();
  TestScreenCanPopItselfDuringUpdate();
  TestBackGoesTopDown();
  TestFadeTransition();
  TestClearExitsEveryone();
  if (auto font = wlw::ui::Font::Load(WLW_TEST_FONT)) TestButtonClicks(*font);
  return TestResult("screen");
}
