# WLWengine

[![CI](https://github.com/aboelbisher/WLWEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/aboelbisher/WLWEngine/actions/workflows/ci.yml)

A small C++20 / OpenGL graphics engine for simple games: Windows, macOS, Linux and Android (iOS next).
It's a plain CMake library with no IDE-specific project files, so Visual Studio, Xcode, CLion, VS Code and (later) Android Studio all work from the same `CMakeLists.txt`.

## Layout

| Path | What |
|---|---|
| `engine/wlw/` | Engine source. Include as `#include "wlw/scene/window.h"` |
| `engine/wlw/platform/` | The only per-OS code: `glfw/` (desktop), `android/` (NativeActivity + EGL). Everything else is shared |
| `samples/` | Small sample games (Snake) built when the engine is the top-level project |
| `third_party/` | Vendored glad (GL loader), stb, tinygltf |
| `cmake/WLWHelpers.cmake` | `wlw_add_assets()`, `wlw_set_warnings()` for game targets |
| `vcpkg/` + `vcpkg.json` | Dependency manager (git submodule, pinned) + manifest (glm, glfw3) |

## Getting the code

```sh
git clone --recursive git@github.com:aboelbisher/WLWEngine.git
cd WLWEngine
git lfs pull
```
(Already cloned? `git submodule update --init --recursive`.)

vcpkg is bootstrapped automatically on the first configure. Dependencies install into `build/<preset>/vcpkg_installed/`, so the first configure takes a few minutes.

## Building

**Visual Studio 2026**: *File → Open → Folder…* on the repo root. Pick the `Windows Debug` preset in the toolbar and build. This also builds the sample games in `samples/`; run `build/windows-debug/samples/snake/wlw_snake.exe`. Samples are skipped when the engine is used as a submodule.

**Xcode**
```sh
cmake --preset xcode
open build/xcode/WLWengine.xcodeproj
```

**Command line** (Developer PowerShell on Windows)
```sh
cmake --preset windows-debug        # or: debug / release on macOS & Linux
cmake --build --preset windows-debug
```

## Platforms

| | Window / context | Graphics | Input |
|---|---|---|---|
| Windows, macOS, Linux | GLFW | OpenGL 3.3 core (glad) | keyboard, mouse |
| Android | NativeActivity + EGL | OpenGL ES 3.0 | touch (swipes, taps), keys |

The renderer is one codebase for both GL flavours (`engine/wlw/rendering/gl.h`); shaders are written
without a `#version` line and get the right one prepended at compile time.

## Using the engine in a game

Games live in their own repos and add the engine as a submodule. [Bluzzle](https://github.com/aboelbisher/Bluzzle) is the current one:

```cmake
add_subdirectory(external/WLWengine)
wlw_add_game(my_game SOURCES main.cpp ASSETS assets)   # .exe on desktop, .so on Android
```

A game is a `wlw::app::System` (it runs for the app's whole life; `Screen`s are its states) and overrides the hooks it needs; `App()` gives the services (window, input, audio, screens, time):

```cpp
class MyGame : public wlw::app::System {
 public:
  void OnStart() override {}                 // build the scene
  void OnFixedUpdate(float dt) override {}   // game logic, constant dt
  void OnUpdate(float dt) override {}        // per-frame: input, camera
  void OnRender(float alpha) override {}     // interpolate state into nodes
};

std::unique_ptr<wlw::app::Application> MakeApp() {
  auto app = std::make_unique<wlw::app::Application>(wlw::app::AppConfig{.title = "My Game"});
  app->AddSystem<MyGame>();
  return app;
}
WLW_MAIN(MakeApp)   // #include "wlw/app/entry_point.h": main() on desktop, android_main() on Android
```

## Git workflow

- Make a branch per feature (`git switch -c feature/x`), commit small and often, merge to `main`.
- Don't `--amend` commits you've already pushed.
