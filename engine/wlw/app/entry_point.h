#pragma once

// WLW_MAIN(MyGameClass) defines the program entry point for the current platform, so a game's
// main.cpp is identical everywhere:
//
//   #include "wlw/app/entry_point.h"
//   #include "my_game.h"
//   WLW_MAIN(MyGame)
//
// Desktop: a normal main(). Android: android_main(), called by NativeActivity on its own thread.
// (iOS will get its own variant here.)

#include <memory>

#include "wlw/app/application.h"

#if defined(WLW_PLATFORM_ANDROID)

struct android_app;
namespace wlw::platform {
void RunAndroidApp(android_app* app, std::unique_ptr<Application> (*create_app)());
}

#define WLW_MAIN(AppClass)                                                                    \
  extern "C" void android_main(struct android_app* app) {                                     \
    ::wlw::platform::RunAndroidApp(app, []() -> std::unique_ptr<::wlw::Application> {         \
      return std::make_unique<AppClass>();                                                    \
    });                                                                                       \
  }

#else

#define WLW_MAIN(AppClass) \
  int main() {             \
    AppClass app;          \
    return app.Run();      \
  }

#endif
