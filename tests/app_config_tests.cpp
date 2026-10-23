// Tests for the app's config (app/application.h AppConfig, ResolveAppConfig): the groups' defaults,
// and the defaults that follow other fields.
#include <string>

#include "test_helpers.h"
#include "wlw/app/application.h"

TEST_SUITE_BEGIN("app_config");

using namespace wlw;
using app::AppConfig;
using app::ResolveAppConfig;

TEST_CASE("app config: defaults, and the window title follows the name") {
  AppConfig config;
  config.name = "Puzzler";
  const AppConfig resolved = ResolveAppConfig(config);
  CHECK((resolved.name == "Puzzler" && resolved.window.title == "Puzzler"));
  CHECK((resolved.window.size.x == 1280.0f && resolved.window.stats_in_title));
  CHECK((resolved.graphics.target_fps == 60 && resolved.graphics.screen_depth));
  CHECK((resolved.audio.enabled && resolved.audio.max_voices == 24));
  CHECK((Near(resolved.time.fixed_step, 1.0f / 60.0f) && Near(resolved.time.max_frame_time, 0.25f)));
  CHECK(resolved.ui.reference_size.x == 640.0f);

  config.window.title = "Puzzler (beta)";  // a title of its own stays
  CHECK((ResolveAppConfig(config).window.title == "Puzzler (beta)" && ResolveAppConfig(config).name == "Puzzler"));
}

TEST_SUITE_END();
