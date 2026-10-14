# Engine microbenchmarks (nanobench): one binary, wlw_benchmarks, no window or GPU. Built when the engine
# is the top-level project, Android included (run it over adb). Numbers only mean something from an optimized
# build (windows-release / windows-profile / release / android-arm64-profile). See bench_main.cpp for options.
add_executable(wlw_benchmarks
  bench.h
  bench_main.cpp
  fx_benchmarks.cpp
  render_benchmarks.cpp
  ui_and_audio_benchmarks.cpp)
target_link_libraries(wlw_benchmarks PRIVATE wlw::wlw wlw_nanobench wlw_json)
target_compile_definitions(wlw_benchmarks PRIVATE WLW_BENCH_BUILD_TYPE="$<CONFIG>")
if(NOT ANDROID)
  target_compile_definitions(wlw_benchmarks PRIVATE WLW_BENCH_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
endif()
wlw_set_warnings(wlw_benchmarks)

# Every benchmark runs a few times in the normal test runs (label bench), unmeasured: they keep compiling and
# working, and ASan/UBSan check them too.
if(WLW_BUILD_TESTS)
  add_test(NAME "bench: smoke" COMMAND wlw_benchmarks --smoke)
  set_tests_properties("bench: smoke" PROPERTIES LABELS bench)
endif()
