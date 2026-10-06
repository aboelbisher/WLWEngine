# WLWengine

A small C++20 / OpenGL graphics engine for simple games: Windows, macOS, Linux and Android (iOS next).
It's a plain CMake library with no IDE-specific project files, so Visual Studio, Xcode, CLion, VS Code and (later) Android Studio all work from the same `CMakeLists.txt`.

## Layout

| Path | What |
|---|---|
| `engine/wlw/` | Engine source. Include as `#include "wlw/scene/window.h"` |
| `engine/wlw/platform/` | The only per-OS code: `glfw/` (desktop), `android/` (NativeActivity + EGL). Everything else is shared |
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

**Visual Studio 2026**: *File → Open → Folder…* on the repo root. Pick the `Windows Debug` preset in the toolbar and build. The engine is a library only; to run something, open a game that uses it, like [WLWSnake](https://github.com/aboelbisher/WLWSnake).

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

Games live in their own repos and add the engine as a submodule. [WLWSnake](https://github.com/aboelbisher/WLWSnake) is the reference game:

```cmake
add_subdirectory(external/WLWengine)
wlw_add_game(my_game SOURCES main.cpp ASSETS assets)   # .exe on desktop, .so on Android
```

A game derives from `wlw::Application` and overrides its hooks:

```cpp
class MyGame : public wlw::Application {
 protected:
  void OnStart() override {}                 // build the scene
  void OnFixedUpdate(float dt) override {}   // game logic, constant dt
  void OnUpdate(float dt) override {}        // per-frame: input, camera
  void OnRender(float alpha) override {}     // interpolate state into nodes
};

WLW_MAIN(MyGame)   // #include "wlw/app/entry_point.h": main() on desktop, android_main() on Android
```

## Git workflow

- Make a branch per feature (`git switch -c feature/x`), commit small and often, merge to `main`.
- Don't `--amend` commits you've already pushed.
