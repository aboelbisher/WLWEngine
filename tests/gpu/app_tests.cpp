// Tests for the Application's frame loop in three calls (app/application.h: Internal().Init / Frame /
// Shutdown, Run, Quit): hook order, quitting with a code, the lifecycle checks; paused frames and Step; a
// session hosted on another app (the Studio's Play) on a scene::OffscreenWindow. A real app on a hidden
// window (it needs a GL context, so these run with the GPU tests: Linux CI and local machines). Its own
// binary, wlw_app_tests (see tests/gpu/CMakeLists.txt).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include <vector>

#include "wlw/app/application.h"
#include "wlw/rendering/rendering_driver.h"
#include "wlw/scene/offscreen_window.h"
#include "wlw/ui/ui_context.h"
#if WLW_DEBUG_UI
#include "wlw/debug/debug_ui.h"
#endif

TEST_SUITE_BEGIN("app");

using namespace wlw;
using app::Application;

namespace {

using HookLog = std::vector<std::string>;

// Writes its hooks into a list, and does what a test asks of it in OnUpdate.
class Recorder : public app::System {
 public:
  Recorder(HookLog* hooks, std::string label) : hooks_(hooks), label_(std::move(label)) {}

  std::function<void(Application&, int frame)> on_update;  // `frame` counts from 1
  int shutdowns = 0;
  float last_dt = -1.0f;  // OnUpdate's
  ui::Rect safe_area{};   // OnRenderUI's
  Vec2 canvas_size{};     // the UI canvas's

  void OnStart() override { Add("start"); }
  void OnFixedUpdate(float) override { Add("fixed"); }
  void OnUpdate(float dt) override {
    ++frame_;
    last_dt = dt;
    Add("update");
    if (on_update) on_update(App(), frame_);
  }
  void OnRender(float) override { Add("render"); }
  void OnRenderUI(ui::UIContext& ui) override {
    safe_area = ui.safe_area;
    canvas_size = ui.canvas.Size();
    Add("ui");
  }
  void OnDebugUI() override { Add("debug"); }
  void OnShutdown() override {
    ++shutdowns;
    Add("shutdown");
  }

 private:
  void Add(const char* hook) { hooks_->push_back(label_ + " " + hook); }

  HookLog* hooks_;
  std::string label_;
  int frame_ = 0;
};

std::unique_ptr<Application> MakeTestApp() {
  app::AppConfig config;
  config.name = "wlw_app_tests";
  config.window = {.size = {64, 64}, .stats_in_title = false, .visible = false};
  config.audio.enabled = false;
  config.time.fixed_step = 1.0f / 1000.0f;  // a fixed step in (almost) every frame
  return std::make_unique<Application>(config);
}

int Count(const HookLog& hooks, const std::string& entry) {
  int count = 0;
  for (const std::string& line : hooks) count += line == entry ? 1 : 0;
  return count;
}

}  // namespace

TEST_CASE("app: OnStart in Init, then each frame's hooks in order") {
  HookLog hooks;
  auto app = MakeTestApp();
  Recorder& recorder = app->AddSystem<Recorder>(&hooks, "a");
  REQUIRE_MESSAGE(app->Internal().Init(), "no GL 3.3 context (on Linux CI: run under xvfb-run with Mesa)");
  CHECK(hooks == HookLog{"a start"});

  hooks.clear();
  CHECK(app->Internal().Frame());
  REQUIRE(hooks.size() >= 3);
  CHECK(hooks.back() == "a ui");
  CHECK(hooks[hooks.size() - 2] == "a render");
  CHECK(hooks[hooks.size() - 3] == "a update");
  for (size_t i = 0; i + 3 < hooks.size(); ++i) CHECK(hooks[i] == "a fixed");  // fixed steps first

  CHECK(app->Internal().Shutdown() == 0);
  CHECK(recorder.shutdowns == 1);
}

TEST_CASE("app: Quit(code) from a hook finishes that frame, Shutdown returns the code") {
  HookLog hooks;
  auto app = MakeTestApp();
  Recorder& recorder = app->AddSystem<Recorder>(&hooks, "a");
  recorder.on_update = [](Application& running, int frame) {
    if (frame == 2) running.Quit(3);
  };
  REQUIRE(app->Internal().Init());
  CHECK(app->Internal().Frame());
  hooks.clear();
  CHECK_FALSE(app->Internal().Frame());  // the frame Quit was called in...
  CHECK(hooks.back() == "a ui");         // ...still ran to the end
  CHECK_FALSE(app->Internal().Frame());  // and no more frames run
  CHECK(Count(hooks, "a update") == 1);
  CHECK(app->Internal().Shutdown() == 3);
  CHECK(recorder.shutdowns == 1);
  CHECK(app->Internal().Shutdown() == 3);  // once only
  CHECK(recorder.shutdowns == 1);
}

TEST_CASE("app: Frame before Init and after Shutdown, Init twice") {
  HookLog hooks;
  auto app = MakeTestApp();
  app->AddSystem<Recorder>(&hooks, "a");
  CHECK_FALSE(app->Internal().Frame());
  CHECK(hooks.empty());
  REQUIRE(app->Internal().Init());
  CHECK_FALSE(app->Internal().Init());  // refused (logged)
  CHECK(Count(hooks, "a start") == 1);
  CHECK(app->Internal().Shutdown() == 0);
  hooks.clear();
  CHECK_FALSE(app->Internal().Frame());
  CHECK(hooks.empty());
}

TEST_CASE("app: Frame and Shutdown from inside a frame are refused") {
  HookLog hooks;
  auto app = MakeTestApp();
  Recorder& recorder = app->AddSystem<Recorder>(&hooks, "a");
  bool nested_frame = true;
  recorder.on_update = [&](Application& running, int) {
    nested_frame = running.Internal().Frame();
    running.Internal().Shutdown();
  };
  REQUIRE(app->Internal().Init());
  CHECK(app->Internal().Frame());
  CHECK_FALSE(nested_frame);
  CHECK(Count(hooks, "a update") == 1);
  CHECK(recorder.shutdowns == 0);
  CHECK(app->Internal().Frame());  // still running
  CHECK(app->Internal().Shutdown() == 0);
  CHECK(recorder.shutdowns == 1);
}

TEST_CASE("app: destroying a running app shuts it down") {
  HookLog hooks;
  {
    auto app = MakeTestApp();
    app->AddSystem<Recorder>(&hooks, "a");
    REQUIRE(app->Internal().Init());
    CHECK(app->Internal().Frame());
  }
  CHECK(Count(hooks, "a shutdown") == 1);
  CHECK(hooks.back() == "a shutdown");
}

TEST_CASE("app: a system added during a frame starts at the next one") {
  HookLog hooks;
  auto app = MakeTestApp();
  Recorder& recorder = app->AddSystem<Recorder>(&hooks, "a");
  recorder.on_update = [&hooks](Application& running, int frame) {
    if (frame == 1) running.AddSystem<Recorder>(&hooks, "b");
  };
  REQUIRE(app->Internal().Init());
  CHECK(app->Internal().Frame());
  CHECK(Count(hooks, "b start") == 0);
  CHECK(Count(hooks, "b update") == 0);
  hooks.clear();
  CHECK(app->Internal().Frame());
  REQUIRE(!hooks.empty());
  CHECK(hooks.front() == "b start");  // before anything else in that frame
  CHECK(Count(hooks, "b update") == 1);
}

TEST_CASE("app: Run returns the exit code") {
  HookLog hooks;
  auto app = MakeTestApp();
  Recorder& recorder = app->AddSystem<Recorder>(&hooks, "a");
  recorder.on_update = [](Application& running, int frame) {
    if (frame == 3) running.Quit(7);
  };
  CHECK(app->Run() == 7);
  CHECK(Count(hooks, "a update") == 3);
  CHECK(recorder.shutdowns == 1);
}

TEST_CASE("app: paused frames draw but the game doesn't move; Step moves it one fixed step") {
  HookLog hooks;
  auto app = MakeTestApp();
  Recorder& recorder = app->AddSystem<Recorder>(&hooks, "a");
  REQUIRE(app->Internal().Init());
  CHECK(app->Internal().Frame());

  app->Internal().SetPaused(true);
  CHECK(app->Internal().IsPaused());
  hooks.clear();
  CHECK(app->Internal().Frame());
  CHECK(hooks.empty());  // no fixed steps, OnUpdate, OnRender or OnRenderUI
  CHECK(app->Time().Delta() == 0.0f);

  app->Internal().Step();
  CHECK(app->Internal().Frame());
  CHECK(hooks == HookLog{"a fixed", "a update", "a render", "a ui"});
  CHECK(recorder.last_dt == doctest::Approx(1.0f / 1000.0f));  // the fixed step
  hooks.clear();
  CHECK(app->Internal().Frame());  // paused again
  CHECK(hooks.empty());

  // Escape while paused: the game doesn't see it (no Back, no quitting).
  app->Window().GetInput().Internal().OnKey(input::Key::Escape, true);
  CHECK(app->Internal().Frame());
  app->Window().GetInput().Internal().OnKey(input::Key::Escape, false);

  app->Internal().SetPaused(false);
  CHECK(app->Internal().Frame());
  CHECK(Count(hooks, "a update") == 1);
  CHECK(app->Internal().Shutdown() == 0);
}

TEST_CASE("app: a session hosted on another app runs on an offscreen window") {
  HookLog hooks;
  auto host = MakeTestApp();  // the Studio
  Recorder& studio = host->AddSystem<Recorder>(&hooks, "host");
  REQUIRE(host->Internal().Init());
#if WLW_DEBUG_UI
  host->DebugUi().SetOpen(true);
#endif

  // Play: the game's own app on a simulated phone screen.
  const std::shared_ptr<scene::OffscreenWindow> window =
      scene::OffscreenWindow::Create({.size = {40, 24}, .safe_area = {.top = 3.0f, .bottom = 2.0f}, .touch = true});
  auto session = MakeTestApp();
  Recorder& game = session->AddSystem<Recorder>(&hooks, "game");
  REQUIRE(session->Internal().InitHosted(*host, window));
  CHECK(Count(hooks, "game start") == 1);
  CHECK(&session->Window() == window.get());
  CHECK(session->Window().GetSize() == Vec2(40.0f, 24.0f));
  CHECK(session->Input().SurfaceSize() == Vec2(40.0f, 24.0f));
  CHECK(session->Input().IsTouchScreen());
  CHECK_FALSE(host->Input().IsTouchScreen());
  session->Window().MainViewport().SetClearColor(Color{1.0f, 0.0f, 0.0f, 1.0f});

  // The host runs the session's frames from one of its hooks, like the Studio.
  bool session_running = true;
  studio.on_update = [&](Application&, int) { session_running = session->Internal().Frame(); };
  hooks.clear();
  CHECK(host->Internal().Frame());
  CHECK(session_running);
  CHECK(Count(hooks, "game update") == 1);
  CHECK(Count(hooks, "game ui") == 1);
#if WLW_DEBUG_UI
  CHECK(Count(hooks, "game debug") == 1);  // its panels went into the host's ImGui frame
#endif

  // Its screen went into the window's texture, at the device's size.
  const std::vector<uint8_t> pixels = session->Internal().Renderer().ReadPixels(*window->Internal().ScreenTarget());
  REQUIRE(pixels.size() == size_t{40} * 24 * 4);
  CHECK((pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255));
  CHECK((pixels[pixels.size() - 4] == 255 && pixels[pixels.size() - 2] == 0));
  // The UI's safe area is the device's: below its top inset, above its bottom one.
  CHECK(game.safe_area.min.x == 0.0f);
  CHECK(game.safe_area.min.y > 0.0f);
  CHECK(game.safe_area.max.y < game.canvas_size.y);
  CHECK(game.safe_area.max.x == doctest::Approx(game.canvas_size.x));

  // Its input comes from the host: Escape with nothing to close ends the session, not the host.
  window->GetInput().Internal().OnKey(input::Key::Escape, true);
  CHECK(host->Internal().Frame());
  CHECK_FALSE(session_running);
  studio.on_update = nullptr;
  CHECK(session->Internal().Shutdown() == 0);
  CHECK(game.shutdowns == 1);
  CHECK(studio.shutdowns == 0);
  session.reset();  // Stop
  CHECK(host->Internal().Frame());

  // Play again: a fresh app on a fresh window; Quit(code) ends it with that code.
  const std::shared_ptr<scene::OffscreenWindow> window2 = scene::OffscreenWindow::Create({.size = {16, 16}});
  auto session2 = MakeTestApp();
  Recorder& game2 = session2->AddSystem<Recorder>(&hooks, "game2");
  game2.on_update = [](Application& running, int frame) {
    if (frame == 2) running.Quit(4);
  };
  REQUIRE(session2->Internal().InitHosted(*host, window2));
  CHECK_FALSE(session2->Input().IsTouchScreen());
  int frames = 0;
  studio.on_update = [&](Application&, int) {
    ++frames;
    session_running = session2->Internal().Frame();
  };
  CHECK(host->Internal().Frame());
  CHECK(session_running);
  CHECK(host->Internal().Frame());
  CHECK_FALSE(session_running);
  CHECK(frames == 2);
  studio.on_update = nullptr;
  CHECK(session2->Internal().Shutdown() == 4);
  session2.reset();
  CHECK(host->Internal().Frame());
  CHECK(host->Internal().Shutdown() == 0);
  CHECK(studio.shutdowns == 1);
}

TEST_SUITE_END();
