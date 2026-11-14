// Tests for the app's config (app/application.h AppConfig, ResolveAppConfig): the groups' defaults,
// and the defaults that follow other fields.
#include <iterator>
#include <string>

#include "test_helpers.h"
#include "wlw/app/application.h"
#include "wlw/scene/window.h"

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
  CHECK((resolved.graphics.target_fps == 60 && resolved.graphics.screen_depth && resolved.graphics.msaa == 0));
  CHECK((resolved.audio.enabled && resolved.audio.max_voices == 24));
  CHECK((Near(resolved.time.fixed_step, 1.0f / 60.0f) && Near(resolved.time.max_frame_time, 0.25f)));
  CHECK(resolved.ui.reference_size.x == 640.0f);

  config.window.title = "Puzzler (beta)";  // a title of its own stays
  CHECK((ResolveAppConfig(config).window.title == "Puzzler (beta)" && ResolveAppConfig(config).name == "Puzzler"));
}

TEST_CASE("app config: MSAA samples round down to 0, 2, 4 or 8") {
  const int asked[] = {-4, 0, 1, 2, 3, 4, 5, 7, 8, 9, 16};
  const int resolved[] = {0, 0, 0, 2, 2, 4, 4, 4, 8, 8, 8};
  for (size_t i = 0; i < std::size(asked); ++i) {
    AppConfig config;
    config.graphics.msaa = asked[i];
    CAPTURE(asked[i]);
    CHECK(ResolveAppConfig(config).graphics.msaa == resolved[i]);
    CHECK(scene::NormalizeMsaaSamples(asked[i]) == resolved[i]);
  }
  static_assert(scene::NormalizeMsaaSamples(4) == 4);  // usable at compile time
}

TEST_SUITE_END();
