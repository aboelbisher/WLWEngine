# WLWengine: notes for Claude Code

C++20 / OpenGL game engine, built as a CMake static library (`wlw::wlw`). Targets Windows,
macOS, Linux and Android today; iOS is planned. Games live in **separate repos** and include
this repo as a git submodule. The reference game is WLWSnake
(github.com/aboelbisher/WLWSnake). Its `CLAUDE.md` holds the overall project context, the
roadmap and the owner's working preferences: read it too when working from there.

Remote: `git@github.com:aboelbisher/WLWEngine.git`, branch `main`.

## Layout
```
engine/wlw/
  app/        Application (game loop: OnStart/OnFixedUpdate/OnUpdate/OnRender), entry_point.h (WLW_MAIN)
  core/       math types, FixedTimestep, log.h (WLW_LOG_*), logger.h (FAIL_IF), mesh/model, AABB
  input/      Input: keys (pressed vs held), pointer (mouse/first finger), swipes, taps
  platform/   THE ONLY PER-OS CODE: platform.h interface + glfw/glfw_platform.cpp (desktop)
              + android/android_platform.cpp (NativeActivity + EGL). iOS goes here later.
  rendering/  RenderingDriver/RenderDevice interfaces + one GL backend for GL 3.3 core AND GLES 3.0
  scene/      Node (scene graph, local transforms, visibility), Window (surface + scene + input), cameras
  utils/      image/glTF loaders, texture cache, primitives.h (procedural cube/sphere/gem meshes)
third_party/  glad (desktop only), stb, tinygltf + nlohmann json (vendored)
cmake/WLWHelpers.cmake  wlw_add_game(), wlw_add_assets(), wlw_set_warnings()
vcpkg/ + vcpkg.json     desktop deps (glm, glfw3). Android fetches glm via FetchContent instead.
```

## Rules
- **Platform code stays in `engine/wlw/platform/<os>/`.** Nothing else may include GLFW, EGL,
  Android or other OS headers. Shared code talks to `platform.h`, `scene::Window` and `input::Input`.
- **Keep everything GLES 3.0 compatible**, because one renderer serves desktop and mobile:
  - Include GL through `wlw/rendering/gl.h` only (never `<glad/glad.h>` or `<GLES3/gl3.h>` directly).
  - Shader sources have **no `#version` line**. The driver prepends `kGLSLHeader`
    (`#version 330 core` / `#version 300 es` + precision). They must compile as both GLSL 3.30
    and GLSL ES 3.00 (no `texture2D`, no `gl_FragColor`, no desktop-only built-ins).
  - Texture uploads use sized internal formats (`GL_RGB8`, `GL_RGBA8`, `GL_R8`).
  - No desktop-only GL calls (`glPolygonMode`, `GL_QUADS`, double-precision functions, ...).
- **Logging:** use `WLW_LOG_INFO/WARN/ERROR`, not `std::cout`, which goes nowhere on Android.
  Android logcat tag: `WLW`.
- **Files and assets:** read through `platform::ReadFile()`, because Android assets live inside the APK.
- **GPU resource destructors** must check `platform::HasCurrentGLContext()` before calling GL.
- **New source files** must be added to the explicit list in `engine/CMakeLists.txt`.
- **Conventions:** namespaces `wlw::{core,scene,rendering,input,platform,utils}`; classes and
  methods `PascalCase`; variables `snake_case`; private members `snake_case_`; `.clang-format`
  (LLVM, 2 spaces, 120 columns) for new code.
- **Don't delete files without asking the owner first.**

## Building the engine on its own
Windows (from a "Developer PowerShell for VS"):
```
cmake --preset windows-debug
cmake --build --preset windows-debug
```
The engine has no executable of its own. Build and run WLWSnake to test changes.

## Known gaps / tech debt
- The glTF loader still reads files with `fopen`, so glTF models don't load from APK assets yet.
- Lights are stored inside materials, not in the scene. `Window` owns the scene (a separate
  `Scene` class is planned).
- `Window`'s default camera is an `FPSCamera`; games normally replace it with `SetCamera()`.
- Touch input uses only the first finger (no multi-touch yet).
