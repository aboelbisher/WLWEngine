# Changelog

Steps refer to ROADMAP.md. Breaking changes come with migration notes.

## Unreleased

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
