# WLWengine

A small C++20 / OpenGL graphics engine for simple games. Desktop (Windows, macOS, Linux) now; Android and iOS later.
It's a plain CMake library with no IDE-specific project files, so Visual Studio, Xcode, CLion, VS Code and (later) Android Studio all work from the same `CMakeLists.txt`.

## Layout

| Path | What |
|---|---|
| `engine/wlw/` | Engine source. Include as `#include "wlw/scene/window.h"` |
| `third_party/` | Vendored glad (GL loader), stb, tinygltf |
| `examples/` | Sample games (arena, platformer) + their `assets/`. Built only when the engine is the top-level project |
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

**Visual Studio 2026**: *File → Open → Folder…* on the repo root. Pick the `Windows Debug` preset in the toolbar, then pick a target (e.g. `wlw_example_platformer.exe`) and press F5.

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

Assets are copied next to each executable on build, so you can run it from anywhere.

## Using the engine in a game

Games live in their own repos (e.g. [WLWSnake](https://github.com/aboelbisher/WLWSnake)) and add the engine as a submodule:

```cmake
add_subdirectory(external/WLWengine)
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE wlw::wlw)
wlw_add_assets(my_game assets)
```

## Git workflow

- Make a branch per feature (`git switch -c feature/x`), commit small and often, merge to `main`.
- Don't `--amend` commits you've already pushed.
- Binary assets (`*.png`, `*.jpg`, `*.bin`, `*.glb`) are stored in Git LFS (see `.gitattributes`).
