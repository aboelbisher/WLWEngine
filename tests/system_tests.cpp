// Tests for app::SystemList: how the frame's phases reach the systems (order, starting, Back,
// lookup). No window: the Application's loop calls exactly these.
#include <memory>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "wlw/app/system.h"

TEST_SUITE_BEGIN("system");

using wlw::app::System;
using wlw::app::SystemList;

namespace {

std::vector<std::string> g_log;

// Records every hook as "<name>:<hook>".
class Recorder : public System {
 public:
  explicit Recorder(std::string name, bool handles_back = false) : name_(std::move(name)), handles_back_(handles_back) {}
  void OnStart() override { Log("start"); }
  void OnUpdate(float) override { Log("update"); }
  bool OnBack() override {
    Log("back");
    return handles_back_;
  }
  void OnAppPaused() override { Log("paused"); }
  wlw::app::Application* AppPointer() const { return &App(); }

 protected:
  void Log(const char* hook) { g_log.push_back(name_ + ":" + hook); }
  std::string name_;
  bool handles_back_;
};

// A system that adds another one while starting (e.g. a game adding its audio system).
class Spawner : public Recorder {
 public:
  Spawner(SystemList& list) : Recorder("spawner"), list_(list) {}
  void OnStart() override {
    Recorder::OnStart();
    list_.Add(std::make_unique<Recorder>("child"), nullptr);
  }

 private:
  SystemList& list_;
};

class Other : public System {};

}  // namespace

TEST_CASE("order and starting") {
  g_log.clear();
  SystemList list;
  list.Add(std::make_unique<Recorder>("a"), nullptr);
  list.Add(std::make_unique<Recorder>("b"), nullptr);
  // Nothing runs before they're started.
  list.ForEach([](System& s) { s.OnUpdate(0.0f); });
  CHECK((g_log.empty() && list.Started() == 0));

  list.StartPending();
  list.ForEach([](System& s) { s.OnUpdate(0.0f); });
  CHECK((g_log == std::vector<std::string>{"a:start", "b:start", "a:update", "b:update"}));

  // Added while running: started (and updated) from the next StartPending on, never twice.
  g_log.clear();
  list.Add(std::make_unique<Recorder>("c"), nullptr);
  list.ForEach([](System& s) { s.OnUpdate(0.0f); });
  list.StartPending();
  list.StartPending();
  CHECK((g_log == std::vector<std::string>{"a:update", "b:update", "c:start"}));
}

TEST_CASE("added while starting") {
  g_log.clear();
  SystemList list;
  list.Add(std::make_unique<Spawner>(list), nullptr);
  list.Add(std::make_unique<Recorder>("after"), nullptr);
  list.StartPending();
  // The child comes after everything added before it, and is started in the same pass.
  CHECK((g_log == std::vector<std::string>{"spawner:start", "after:start", "child:start"}));
  CHECK((list.Size() == 3 && list.Started() == 3));
}

TEST_CASE("back goes last added first") {
  g_log.clear();
  SystemList list;
  list.Add(std::make_unique<Recorder>("game", /*handles_back=*/true), nullptr);
  list.Add(std::make_unique<Recorder>("overlay"), nullptr);
  list.Add(std::make_unique<Recorder>("debug"), nullptr);
  CHECK(!list.Back());  // none started yet: nobody asked
  list.StartPending();
  g_log.clear();
  CHECK(list.Back());
  CHECK((g_log == std::vector<std::string>{"debug:back", "overlay:back", "game:back"}));

  SystemList nobody;
  nobody.Add(std::make_unique<Recorder>("x"), nullptr);
  nobody.StartPending();
  CHECK(!nobody.Back());  // unhandled: the app goes to the background
}

TEST_CASE("get and app") {
  SystemList list;
  auto* app = reinterpret_cast<wlw::app::Application*>(0x1234);  // only stored, never used here
  Recorder& recorder = static_cast<Recorder&>(list.Add(std::make_unique<Recorder>("r"), app));
  CHECK(recorder.AppPointer() == app);  // App() is set when added
  CHECK(list.Get<Recorder>() == &recorder);
  CHECK(list.Get<System>() == &recorder);  // the first one of a base type
  CHECK(list.Get<Other>() == nullptr);
}

TEST_SUITE_END();
