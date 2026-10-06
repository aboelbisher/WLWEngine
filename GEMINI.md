# WLWengine - C++ Graphics Engine

A modular C++20 graphics engine (OpenGL 3.3 / OpenGL ES 3.0) for games on Windows, macOS, Linux and Android.
See `CLAUDE.md` for the full, up-to-date rules (platform layer, GLES compatibility, logging).

## Architecture Overview

The engine is a CMake static library (`wlw::wlw`). Engine headers are included with the
`wlw/` prefix, e.g. `#include "wlw/scene/window.h"`. Games live in separate repos and
consume the engine as a git submodule (`add_subdirectory` + `target_link_libraries(... wlw::wlw)`).

### Modules (`engine/wlw/`)
- **`core/`**: vectors, color, vertex types, `mesh.h`, `model.h`, `collision.h` (AABB), `logger.h`.
- **`rendering/`**: `rendering_driver.h` / `render_device.h` interfaces with OpenGL implementations
  (`*_gl.cpp`), materials, textures, GPU buffers, embedded shaders in `shaders/`.
- **`app/`**: `Application` (game loop with fixed timestep), `entry_point.h` (`WLW_MAIN`).
- **`input/`**: keys, pointer/touch, swipes, taps.
- **`platform/`**: the only per-OS code (`glfw/` desktop, `android/`).
- **`scene/`**: `node.h` (scene graph, Node2D/Node3D), `window.h` (platform-neutral window interface), cameras.
- **`utils/`**: image + GLTF loaders, texture cache, `primitives.h` (procedural cube/sphere/gem/... meshes).
- **`wlw.h`**: engine entry point (`WLWEngine::Create()`).

## Project Structure
- `engine/` - the library. `third_party/` - glad, stb, tinygltf (vendored).
- No sample games live here; [WLWSnake](https://github.com/aboelbisher/WLWSnake) is the reference game that consumes the engine.
- `cmake/WLWHelpers.cmake` - `wlw_add_game()` (exe on desktop, .so on Android), `wlw_add_assets()`.
- `vcpkg/` (submodule) + `vcpkg.json` - glm, glfw3.

## Coding Standards & Conventions
- **C++ Version**: C++20.
- **Memory Management**: Prefer `std::shared_ptr` and `std::unique_ptr` over raw pointers.
- **Abstractions**: Use abstract base classes (interfaces) for platform-dependent features (e.g., `RenderingDriver`, `Window`).
- **Namespaces**:
  - `wlw`: Root namespace.
  - `wlw::core`, `wlw::scene`, `wlw::rendering`, `wlw::input`, `wlw::platform`, `wlw::utils`: Sub-namespaces for organization.
- **Naming**:
  - Classes: `PascalCase`.
  - Methods: `PascalCase` (e.g., `AddNode`, `SetPosition`).
  - Variables: `snake_case` (e.g., `last_window_id`, `nodes_3d_`).
  - Private Members: `snake_case_` with trailing underscore.

## Building and Running
- CMake 3.21+, presets in `CMakePresets.json`: `windows-debug`, `windows-release`, `debug`, `release`, `xcode`.
- No IDE-specific project files are committed; IDEs open the CMake project directly.
- Assets are loaded relative to the executable's folder (copied there at build time).

## Current Status
The engine supports:
- Loading and rendering 3D models (including GLTF).
- Scene graph hierarchy with parent-child relationships.
- Basic lighting (ambient, shininess) and texturing.
- FPS-style and follow cameras.
- Input: keyboard, mouse, touch (swipes, taps).
- Android (NativeActivity + EGL + GLES 3.0) and desktop (GLFW + GL 3.3).
