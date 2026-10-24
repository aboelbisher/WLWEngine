// Tests for the Application's frame loop in three calls (app/application.h: Internal().Init / Frame /
// Shutdown, Run, Quit): hook order, quitting with a code, the lifecycle checks. A real app on a hidden
// window (it needs a GL context, so these run with the GPU tests: Linux CI and local machines). Its own
// binary, wlw_app_tests (see tests/gpu/CMakeLists.txt).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "wlw/app/application.h"

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

  void OnStart() override { Add("start"); }
  void OnFixedUpdate(float) override { Add("fixed"); }
  void OnUpdate(float) override {
    ++frame_;
    Add("update");
    if (on_update) on_update(App(), frame_);
  }
  void OnRender(float) override { Add("render"); }
  void OnRenderUI(ui::UIContext&) override { Add("ui"); }
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

TEST_SUITE_END();
