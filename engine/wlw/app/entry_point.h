#pragma once

// WLW_MAIN(make_app) defines the program entry point for the current platform, so a game's
// main.cpp is identical everywhere. `make_app` is the game's function that builds its app:
//
//   #include "wlw/app/entry_point.h"
//
//   std::unique_ptr<wlw::app::Application> MakeApp() {
//     auto app = std::make_unique<wlw::app::Application>(MakeConfig());
//     app->AddSystem<MyGame>();
//     return app;
//   }
//   WLW_MAIN(MakeApp)
//
// Desktop: a normal main(). Android: android_main(), called by NativeActivity on its own thread.
// Both call Application::Run(). (iOS and the web will get their variants here; where the platform
// owns the loop they drive Internal().Init() / Frame() / Shutdown() instead: see application.h.)

#include <memory>

#include "wlw/app/application.h"

#if defined(WLW_PLATFORM_ANDROID)

struct android_app;
namespace wlw::platform {
void RunAndroidApp(android_app* app, std::unique_ptr<::wlw::app::Application> (*create_app)());
}

#define WLW_MAIN(make_app) \
  extern "C" void android_main(struct android_app* app) { ::wlw::platform::RunAndroidApp(app, &make_app); }

#else

#include <string>
#include <vector>

namespace wlw::platform {
void SetLaunchArguments(std::vector<std::string> arguments);
}

#define WLW_MAIN(make_app)                                                                                 \
  int main(int argc, char** argv) {                                                                        \
    ::wlw::platform::SetLaunchArguments(std::vector<std::string>(argv + (argc > 0 ? 1 : 0), argv + argc)); \
    std::unique_ptr<::wlw::app::Application> app = make_app();                                             \
    return app ? app->Run() : 1;                                                                           \
  }

#endif
