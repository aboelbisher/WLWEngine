// The Studio on a hidden window (studio/): a StudioSystem on its own Application plays a small test game through
// its PlaySession and Game panel: Play / Pause / Step / Stop, a click in the Game panel reaching the game in device
// pixels, the game quitting by itself. It needs a GL context (the Studio's renderer and ImGui), so it is labelled
// gpu, like wlw_app_tests, and is its own binary for the same reason (tests/studio/CMakeLists.txt).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <memory>

#include "wlw/app/application.h"
#include "wlw/debug/debug_ui.h"
#include "wlw/input/input.h"
#include "wlw/studio/studio.h"

TEST_SUITE_BEGIN("studio_app");

using namespace wlw;
using app::Application;

namespace {

// What the test game saw, kept outside it: each Play makes a fresh game app.
struct GameLog {
  int starts = 0;
  int updates = 0;
  int fixed_updates = 0;
  int shutdowns = 0;
  bool pressed = false;     // the pointer went down in some frame
  Vec2 pressed_at{-1.0f};   // where, in the game's pixels
  Vec2 surface{0.0f};       // the game's screen size when it was pressed
  int quit_at_update = -1;  // > 0: Quit(7) at that update
};
GameLog g_log;

class TestGame : public app::System {
 public:
  void OnStart() override { ++g_log.starts; }
  void OnFixedUpdate(float) override { ++g_log.fixed_updates; }
  void OnUpdate(float) override {
    ++g_log.updates;
    const input::Input& input = App().Input();
    if (input.WasPointerPressed()) {
      g_log.pressed = true;
      g_log.pressed_at = input.PointerPosition();
      g_log.surface = input.SurfaceSize();
    }
    if (g_log.updates == g_log.quit_at_update) App().Quit(7);
  }
  void OnShutdown() override { ++g_log.shutdowns; }
};

std::unique_ptr<Application> MakeTestGame() {
  app::AppConfig config;
  config.name = "wlw_studio_app_tests_game";
  config.audio.enabled = false;
  config.time.fixed_step = 1.0f / 1000.0f;  // a fixed step in (almost) every frame
  auto game = std::make_unique<Application>(config);
  game->AddSystem<TestGame>();
  return game;
}

std::unique_ptr<Application> MakeStudio() {
  app::AppConfig config = studio::StudioConfig("wlw_studio_app_tests");
  config.window.size = {800, 600};
  config.window.visible = false;
  return std::make_unique<Application>(config);
}

}  // namespace

TEST_CASE("studio: Play, a click in the Game panel, Pause, Step, Stop, Play again, the game quits") {
  g_log = {};
  auto host = MakeStudio();
  studio::StudioSystem& studio = host->AddSystem<studio::StudioSystem>("test", &MakeTestGame);
  REQUIRE(host->Internal().Init());
  CHECK(host->DebugUi().StudioMode());
  CHECK(host->DebugUi().IsOpen());
  for (int i = 0; i < 3; ++i) CHECK(host->Internal().Frame());  // the default layout gets built
  CHECK_FALSE(studio.Session().Playing());
  CHECK(studio.Session().CanPlay());

  // Play: takes effect at the next frame, on the Free device (the Game panel's size).
  studio.Play();
  CHECK(host->Internal().Frame());
  REQUIRE(studio.Session().Playing());
  CHECK(g_log.starts == 1);
  CHECK(g_log.updates == 1);
  CHECK(host->Internal().Frame());  // the panel has drawn the picture once now
  const studio::GameRect rect = studio.Game().PictureRect();
  REQUIRE(rect.size.x > 10.0f);
  REQUIRE(rect.size.y > 10.0f);
  const Vec2 device = studio.Session().Window()->GetSize();
  CHECK(rect.size.x / rect.size.y == doctest::Approx(device.x / device.y).epsilon(0.01));

  // A click at the picture's centre: the pointer moves there (ImGui then sees it over the picture), then goes down.
  const Vec2 centre = rect.min + rect.size * 0.5f;
  host->Window().GetInput().Internal().OnPointerMove(centre.x, centre.y);
  CHECK(host->Internal().Frame());
  CHECK(host->Internal().Frame());
  CHECK_FALSE(g_log.pressed);
  host->Window().GetInput().Internal().OnMouseButton(input::MouseButton::Left, true);
  host->Window().GetInput().Internal().OnPointerDown(centre.x, centre.y);
  CHECK(host->Internal().Frame());
  REQUIRE(g_log.pressed);
  CHECK(g_log.surface == device);
  CHECK(g_log.pressed_at.x == doctest::Approx(device.x * 0.5f).epsilon(0.02));
  CHECK(g_log.pressed_at.y == doctest::Approx(device.y * 0.5f).epsilon(0.02));
  host->Window().GetInput().Internal().OnMouseButton(input::MouseButton::Left, false);
  host->Window().GetInput().Internal().OnPointerUp(centre.x, centre.y);
  CHECK(host->Internal().Frame());

  // Pause: the Studio and the panel go on drawing, the game doesn't move. Step: one fixed step.
  studio.TogglePause();
  CHECK(host->Internal().Frame());
  CHECK(studio.Session().Paused());
  const int updates = g_log.updates;
  const int fixed = g_log.fixed_updates;
  for (int i = 0; i < 3; ++i) CHECK(host->Internal().Frame());
  CHECK(g_log.updates == updates);
  CHECK(g_log.fixed_updates == fixed);
  studio.Step();
  CHECK(host->Internal().Frame());
  CHECK(g_log.fixed_updates == fixed + 1);
  CHECK(studio.Session().Paused());
  studio.TogglePause();
  CHECK(host->Internal().Frame());
  CHECK_FALSE(studio.Session().Paused());
  CHECK(host->Internal().Frame());
  CHECK(g_log.updates > updates);

  // Stop: the game shuts down and is gone; the Studio goes on.
  studio.Stop();
  CHECK(host->Internal().Frame());
  CHECK_FALSE(studio.Session().Playing());
  CHECK(g_log.shutdowns == 1);
  CHECK_FALSE(studio.Session().LastExitCode().has_value());
  CHECK(studio.Game().PictureRect().size == Vec2(0.0f));
  CHECK(host->Internal().Frame());

  // Play again: a fresh game, which quits by itself with its code; the Studio stays.
  g_log.updates = 0;
  g_log.quit_at_update = 3;
  studio.Play();
  for (int i = 0; i < 5; ++i) CHECK(host->Internal().Frame());
  CHECK(g_log.starts == 2);
  CHECK_FALSE(studio.Session().Playing());
  CHECK(g_log.shutdowns == 2);
  REQUIRE(studio.Session().LastExitCode().has_value());
  CHECK(*studio.Session().LastExitCode() == 7);

  // Escape with nothing playing doesn't close the Studio.
  host->Window().GetInput().Internal().OnKey(input::Key::Escape, true);
  CHECK(host->Internal().Frame());
  host->Window().GetInput().Internal().OnKey(input::Key::Escape, false);
  CHECK(host->Internal().Frame());

  // Shutting the Studio down while a game plays stops the game first.
  g_log.quit_at_update = -1;
  studio.Play();
  CHECK(host->Internal().Frame());
  REQUIRE(studio.Session().Playing());
  CHECK(host->Internal().Shutdown() == 0);
  CHECK(g_log.shutdowns == 3);
}

TEST_CASE("studio: a Studio with no game can't play") {
  auto host = MakeStudio();
  studio::StudioSystem& studio = host->AddSystem<studio::StudioSystem>("", nullptr);
  REQUIRE(host->Internal().Init());
  CHECK_FALSE(studio.Session().CanPlay());
  studio.Play();
  CHECK(host->Internal().Frame());
  CHECK(host->Internal().Frame());
  CHECK_FALSE(studio.Session().Playing());
  CHECK(host->Internal().Shutdown() == 0);
}

TEST_SUITE_END();
