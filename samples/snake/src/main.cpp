#include <memory>

#include "snake_app.h"
#include "wlw/app/entry_point.h"

// The app: the engine's services + Snake as its one system.
std::unique_ptr<wlw::app::Application> MakeApp() {
  auto app = std::make_unique<wlw::app::Application>(snake::SnakeApp::MakeConfig());
  app->AddSystem<snake::SnakeApp>();
  return app;
}

// Defines main() on desktop and android_main() on Android.
WLW_MAIN(MakeApp)
