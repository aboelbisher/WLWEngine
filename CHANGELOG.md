# Changelog

Steps refer to ROADMAP.md. Breaking changes come with migration notes.

Versions follow SemVer below 1.0: a minor version may break the API, a patch only fixes. One minor version per
finished roadmap phase, each tagged `vX.Y.Z` on `main` with a GitHub Release; games pin their engine submodule
to a tag. Renamed or replaced APIs keep a `WLW_DEPRECATED` shim for one release (listed under **Deprecated**),
then it's deleted (listed under **Removed**).

## Unreleased

## 0.1.0 (2026-10-15): Phase 0, safety net and tooling

The first tagged version: tests (doctest, golden images, benchmarks), CI on Windows, Linux and Android, logging,
asserts, cvars and a console, the Tracy profiler, build hygiene (warnings, optimized engine in Debug, PCH,
vendored dependencies, clang-format) and versioning. Details per step below.

- **Breaking** (migration notes in the steps): `wlw/core/logger.h` and `FAIL_IF` removed (0.4); `tests/check.h`
  removed (0.1). Code that got `<sstream>` through `wlw/scene/node.h` includes it itself (0.4).
- **Deprecated:** nothing yet.

### Versioning (Roadmap 0.9)
- `wlw/version.h`, generated from `project(VERSION)` in the top-level `CMakeLists.txt` (template
  `engine/wlw/version.h.in`): `WLW_VERSION_MAJOR/MINOR/PATCH`, `WLW_VERSION_STRING`, `WLW_VERSION` (one comparable
  number: major * 10000 + minor * 100 + patch), `WLW_MAKE_VERSION` and `WLW_VERSION_AT_LEAST(major, minor, patch)`
  for game code that has to build against two engine versions.
- `wlw/core/build_info.h`: `GetBuildInfo()` (version, git commit, dirty flag, build type) and `BuildInfoText()`
  ("WLWengine 0.1.0 (dfda300c49-dirty, Debug)"). The git commit is read on every build by `cmake/GitInfo.cmake`
  (target `wlw_git_info`), which rewrites its generated source only when the commit or dirty flag changes, so an
  unchanged tree rebuilds nothing. "unknown" without git. The application logs it at startup (first line of a
  debug build's `log.txt`); the engine panel shows it.
- `wlw/core/deprecated.h`: `WLW_DEPRECATED(0.3, "Use X()")` (a `[[deprecated]]` naming the version and the
  replacement), `WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN/END` for code that must still use a shim (its tests), and
  the CMake option `WLW_NO_DEPRECATED` (off) that compiles every shim out, so a game can check it has migrated.
- `cmake/CheckDeprecations.cmake` (CTest test `deprecations`, plus tests of the script itself): every shim in
  `engine/` and `kit/` must name the current or the next minor version, so a shim ships in exactly one release
  and has to be deleted before the one after.
- Fixed (on `main` before this step): MSVC Release warning C4723 in `IntersectPlane` broke the Windows Release
  CI job since 0.8.

Migration: none needed.

### Build hygiene (Roadmap 0.8)
- Warnings: `wlw_set_warnings` (engine, kit, tests, benchmarks, and games through `wlw_add_game`) is now MSVC
  `/W4 /permissive-`, clang/gcc `-Wall -Wextra -Wshadow`. Two warnings stay off: unused parameters (virtual hooks)
  and missing field initializers (configs filled with designated initializers that leave defaults out on purpose).
  The warnings this found are fixed (`primitives.h` double->float, `ShaderMaterial::SetParam` parameter hiding
  `Material::name`, `audio.cpp` bool vs `ma_bool32`, an unused variable in non-profile builds).
- `WLW_WARNINGS_AS_ERRORS` (`/WX`, `-Werror`): off by default, on in every engine preset (so in CI). Files that
  only compile third-party implementations (`miniaudio_impl.cpp`, `libs_impl.cpp`) build without warnings.
- `WLW_OPTIMIZE_ENGINE_IN_DEBUG`: in Debug builds, the engine, kit and third_party compile at `-O2 -g` (MSVC
  `/O2 /Ob2`, no `/RTC1`), while the game, tests, samples and benchmarks stay at -O0 (like Unreal's DebugGame). The
  Debug runtime and `WLW_ASSERT` stay on. On by default when the engine is a submodule; off when it's the
  top-level project. Debug-build benchmarks on the owner's PC: `render/gather 10k` 16.2 ms -> 0.81 ms,
  `fx/particles instances 5k` 8.7 ms -> 0.62 ms, `ui/text 1k glyphs` 0.57 ms -> 0.18 ms. Header-only code (`node.h`
  transforms, templates) compiles inside the caller and keeps the caller's flags.
- `WLW_PCH` (on): the engine precompiles the standard library and glm headers it uses most. Clean `wlw` Debug
  build 6.8 s -> 4.6 s on the owner's PC (one job: 41 s -> 22 s). The Android CI job builds with it off, so a
  missing `#include` still fails there. ccache in CI gets the sloppiness settings PCH needs.
- glm 1.0.1 is vendored in `third_party/glm` (target `wlw_glm`, public through `wlw::wlw`), per D13. vcpkg now
  provides only GLFW; Android no longer downloads glm with FetchContent.
- clang-format: `cmake -P cmake/ClangFormat.cmake` formats `engine/`, `kit/`, `tests/`, `benchmarks/` and
  `samples/` with a pinned clang-format 23.1.3 (downloaded once into `build/tools/`, nothing to install);
  `-DCHECK=ON` only checks. A new CI job `clang-format` runs the check. `.clang-format` now describes the style
  the code was written in (Google-based, 120 columns, `T& x`, short `if`/`case`/functions on one line, includes
  not reordered). Every file was reformatted once, in its own commit, listed in `.git-blame-ignore-revs`.
- CI: actions moved to their Node 24 versions (checkout v7, cache v6, upload-artifact v7, ccache-action 1.2.24);
  `ilammy/msvc-dev-cmd` (still Node 20) replaced by a step that runs VS's `vcvars64.bat`.

Migration: none needed. Games get the stricter warnings as warnings (not errors) and the optimized engine in
Debug builds; `-DWLW_OPTIMIZE_ENGINE_IN_DEBUG=OFF` steps through engine code at -O0. Games can drop `glm`
from their `vcpkg.json`.

### Benchmarks (Roadmap 0.7)
- `benchmarks/`: `wlw_benchmarks`, engine microbenchmarks on nanobench 4.3.11 (vendored in `third_party/nanobench`,
  MIT). No window, no GPU, fixed seeds; one op = one frame's worth of the work. Covered so far: `render/gather`,
  `render/cull` and `render/sort` at 1k and 10k nodes, `transform/move flat|nested` at 1k and 10k,
  `fx/flow update|write 20k`, `fx/particles update|instances 5k`, `ui/text 1k glyphs`, `audio/mix 24 voices`.
  A file adds a group with `WLW_BENCHMARK("group") { ctx.Run("group/name", [&] { ... }); }`.
- `wlw_benchmarks [filter] --out run.json --baseline base.json [--markdown file] [--threshold 25]` writes the
  results as JSON and compares them with a baseline (baseline, now, % change). A regression past the threshold
  is a warning, never a failure. `--smoke` runs each benchmark a few times without measuring: the CTest test
  `bench: smoke` (label `bench`) does that in every test run.
- Baselines in `benchmarks/baselines/`: `windows-local.json` (Ryzen 7 9800X3D, MSVC Release), `pixel4.json`
  (Pixel 4, NDK clang Release, pinned to the prime core with `taskset 80`), `linux-ci.json` (GitHub runner,
  clang Release).
- CI: a new workflow `bench.yml` (Linux clang Release) runs the benchmarks on main, on manual runs and on
  `bench/**` branches. It puts the comparison in the job summary and uploads the JSON as the `bench-linux-ci` artifact.
- Android: `wlw_benchmarks` builds as an arm64 executable, to run over `adb push` + `adb shell`.
- Renderer: the GL-free part of drawing a viewport moved out of the GL driver into
  `wlw/rendering/scene_gather.h`. `GatherScene` walks the scene (lights, drawables with world boxes, sprite
  batches, shadow caster / receiver boxes). `CullScene` does frustum culling, model and normal matrices, and view
  depths. The driver keeps the program and texture lookups. Pictures are unchanged (golden images).

Migration: none needed.

### Tracy profiler (Roadmap 0.6)
- `wlw/core/profile.h`: `WLW_PROFILE_SCOPE("name")`, `WLW_PROFILE_FUNCTION()`, `WLW_PROFILE_SCOPE_DYNAMIC(name)`,
  `WLW_PROFILE_TEXT`, `WLW_PROFILE_FRAME`, `WLW_PROFILE_PLOT`, `WLW_PROFILE_THREAD_NAME`, `WLW_PROFILE_MESSAGE`.
  They feed Tracy 0.14.1 (vendored in `third_party/tracy`, BSD-3) and, on Android, ATrace sections and counters
  (Perfetto / Android Studio). Built in only with the CMake option `WLW_PROFILE` (off by default everywhere);
  without it the macros are empty and their arguments aren't evaluated. `TRACY_ON_DEMAND`: nothing is recorded
  until a viewer connects. A profile build takes ~0.5 s longer to start (Tracy calibrates its timer).
- New presets `windows-profile` and `android-arm64-profile`: RelWithDebInfo (optimized, with symbols), Tracy on,
  debug UI on.
- Zones: every Application phase; each system's hooks, named after the system's type (`System::Name()`, set by
  `AddSystem` through the new `core::TypeName<T>()`); the renderer (draw window, each viewport, gather, sort,
  upload, shadow pass, main pass, canvas, each post pass by name, overlays, present, shader warm-up and every
  program compile); input polling; the audio callback and decode thread (named threads); particle updates; file
  reads, image and glTF loads. Frame marks after present. Plots: frame us, draw calls, program changes, texture
  binds, instance bytes, mixer voices, quality level, GPU us. Log records show on the timeline while a viewer is
  connected.
- GPU zones (`wlw/rendering/gl_gpu_profile.h`, GL backend): Tracy's OpenGL timestamp zones for the shadow pass,
  main pass, canvas, post passes and overlays. On GLES they need EXT_disjoint_timer_query (looked up at run time).
  The query context is made when a viewer first connects. The cvar `profile.gpu_zones` turns them off.
- Engine panel: "Profiler: Tracy connected / waiting" in profile builds.
- CI: the Android job builds with `WLW_PROFILE=ON`.

Migration: none needed. `SystemList::Add` takes an optional name.

### Console variables and console (Roadmap 0.5)
- `wlw/core/cvar.h`: `CVar<bool|int|float|std::string>{"r.name", default, "help", flags, range}`, with dotted
  lowercase names. Flags: `Saved` (kept in `<save dir>/cvars.cfg`), `ReadOnly` (only before the game starts),
  `Cheat` (changeable from outside the code only in debug builds). `Get`, `Set`, `Reset`, `OnChange`;
  `FindCVar`, `AllCVars`, `SetCVar`.
- Where values come from, later ones winning: the code default, `wlw.cfg` in the game's assets, the player's
  `cvars.cfg`, the command line (`game.exe +r.name 0 +log.filter=Render=Verbose`) or, on Android,
  `adb shell am start -n <package>/<activity> --es wlw "r.name=0;log.filter=Render=Verbose"`, then the console.
  A value for a name that doesn't exist yet waits for it; at startup an unknown name is logged as a warning.
- `wlw/core/console.h`: `ConsoleCommand` (registered while it exists), `ExecuteConsoleLine` (a command,
  `cvar`, `cvar value`, `cvar = value`), built-in `help`, `find`, `set`, `reset`, `exec`, and `quit` from
  the Application. Command output goes to the log (category Console).
- Engine cvars (`wlw/app/engine_cvars.h`): `log.filter`, `r.gpu_timing`, `r.invalidate_framebuffers`,
  `quality.lock`. The Engine panel's checkboxes and quality buttons now set them.
- Debug UI: a console window (the ` key or the "Console" button) with the log, level and text filters, and
  an input line with history and Tab completion. A "CVars" panel lists every cvar by prefix, with widgets
  for its type and a reset button. ImGui now gets the keyboard and typed text. On phones the console only
  shows the log; typing there needs GameTextInput (roadmap D9).
- Input: new keys `Tab`, `Backspace`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Control`, `Shift`,
  `Grave`; `Text()` (typed characters); `SetKeyboardBlocked` and `Raw*` key queries (the game's keys are
  blocked while the console has the keyboard).
- Platform: `LaunchArguments()` (desktop `argv`; Android the launch intent's `wlw` extra). `WLW_MAIN`'s
  `main` now takes `argc` and `argv`.

Migration: none needed. Code that switches over `input::Key` without a `default` gets new cases to handle.

### Logging and asserts (Roadmap 0.4)
- `WLW_LOG(Category, Level, "fmt", ...)`: levels Verbose, Debug, Info, Warning, Error, Fatal; engine categories
  `LogCore`, `LogApp`, `LogRender`, `LogShader`, `LogAudio`, `LogPlatform`, `LogUI`, `LogFX` (kit: `LogKit`); games
  declare their own with `WLW_LOG_CATEGORY(LogMyGame)`. Every engine message now has its category.
- Levels below `WLW_LOG_COMPILE_LEVEL` (Debug in debug builds, Info in release) compile to nothing.
- Runtime filters: `SetLogFilter("Render=Verbose,Audio=Warning,*=Info")`, `SetLogLevel`; on desktop the
  `WLW_LOG` environment variable sets one at startup.
- Records carry time, thread id (`T0` = main), category, level and file:line. Desktop lines read
  `[12.345][T0][Render][warning] text`; logcat gets `[Render] text` with the matching priority.
- Sinks: the platform output, an in-memory history of the last 512 records (`RecentLogs`), a log file
  (`StartLogFile`, flushed every line; debug builds write `<save dir>/log.txt` and keep the run before as
  `log.txt.prev`), and `AddLogSink` callbacks. Any thread may log. A Fatal record stops the program after
  it's written everywhere.
- `wlw/core/assert.h`: `WLW_ASSERT` (debug builds; release with the CMake option `WLW_ENABLE_ASSERTS`),
  `WLW_VERIFY` (always evaluated: logs an Error and returns false), `WLW_UNREACHABLE`, and
  `SetAssertHandler` for tests. A debugger, if attached, breaks on failures.
- Platform: `platform::WriteLog(const LogRecord&)`, `IsDebuggerAttached()`, `BreakIntoDebugger()`.
- Removed `wlw/core/logger.h` (`FAIL_IF`).

Migration:
- `WLW_LOG_INFO/WARN/ERROR` and `wlw::Log` still work (category General).
- `if (FAIL_IF(bad, "msg " << x)) return;` becomes `if (!WLW_VERIFY(!bad, "msg %s", x)) return;`. The condition
  is inverted (VERIFY states what must hold), and the message is printf-style. Include `wlw/core/assert.h`.
- Code that got `<sstream>` through `wlw/scene/node.h` (it included `logger.h`) must include it itself.

### Golden images (Roadmap 0.3)
- `tests/gpu`: a new test binary `wlw_gpu_tests` (CTest label `gpu`) draws 7 fixed scenes (lit cubes, shadows,
  transparency, instancing, sprites, bloom, canvas shapes + text + images) into an offscreen viewport of a
  hidden window and compares them with `tests/gpu/references/*.png`. On a mismatch the actual picture and a
  diff (bad pixels in red) go to `build/<preset>/tests/gpu/gpu_output/`; `wlw_gpu_tests --update` rewrites
  the references. References come from Mesa llvmpipe (strict); a real GPU compares more loosely.
- CI runs them on Linux under xvfb with llvmpipe and uploads the output when they fail. Windows CI skips them.
  CI no longer runs for pushes that change only Markdown files.
- `tests/support` (`wlw_test_support`): PNG load/save and image comparison, unit-tested in `wlw_tests`.
- Engine: `Window::Create(size, title, WindowOptions{.visible = false})` makes a hidden window (desktop);
  `RenderingDriver::ReadPixels(RenderTarget&)` reads a target back as RGBA8, top row first;
  `RenderingDriver::GpuName()`.

Migration: none; `Window::Create`'s new argument has a default. Code that implements `RenderingDriver` (only
the engine's GL driver does) must add `ReadPixels` and `GpuName`.

### CI (Roadmap 0.2)
- GitHub Actions (`.github/workflows/ci.yml`) on every push: Windows MSVC Debug (Release too on `main` and
  manual runs), Linux clang Debug with ASan + UBSan, Android arm64 (NDK 30, API 24: engine, kit and Snake).
  Every test runs (`ctest -j`); vcpkg packages and ccache are cached; a newer push cancels the older run.
- New presets: `linux-asan`, `android-arm64` (needs `ANDROID_NDK_HOME`) and test presets
  (`ctest --preset <name>`), so each CI job runs the same way locally.
- `WLW_SANITIZE` (`address;undefined`, off by default) builds every target with those sanitizers.
- `WLW_BUILD_SAMPLES` works on Android (off by default there); Snake builds as `libwlw_snake.so`.
- Linux: the engine links pthread, dl and m (miniaudio).

### Tests (Roadmap 0.1)
- The engine tests use doctest 2.4.12 (vendored in `third_party/doctest`). The 24 suites are one binary,
  `wlw_tests` (one `TEST_SUITE` per file, a shared main in `tests/test_main.cpp`); every `TEST_CASE` is
  its own CTest test (`doctest_discover_tests`), so `ctest -j` runs cases in parallel and a failure
  names the case and prints both values.
- Snake's rule tests use doctest too (`snake_game_tests`, CTest names `snake: <case>`).
- Removed `tests/check.h`; `Near()` moved to `tests/test_helpers.h`.

Migration (only for code that wrote tests against `check.h`): include `test_helpers.h`, turn each test
function into `TEST_CASE("...")`, drop `main()`, and put compound conditions in double parentheses:
`CHECK((a && b))`.
