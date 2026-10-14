# Unit tests for engine code that runs without a window or GPU (math incl. exact cover, input, audio mixing, animation, particles, meshes, UI layout, screens).
# Built only when the engine is the top-level project. One doctest binary, one TEST_SUITE per file; every TEST_CASE is
# its own CTest test. Run: ctest --test-dir build/<preset> (-R <case name>), or wlw_tests -ts=<suite> / -tc=<case>.
add_executable(wlw_tests
  test_main.cpp
  test_helpers.h
  anim_tests.cpp
  audio_tests.cpp
  blend_tests.cpp
  core_tests.cpp
  exact_cover_tests.cpp
  fx_tests.cpp
  gf2_tests.cpp
  gpu_timer_tests.cpp
  input_tests.cpp
  kit_tests.cpp
  lighting_tests.cpp
  picking_tests.cpp
  post_tests.cpp
  primitives_tests.cpp
  quality_tests.cpp
  render_list_tests.cpp
  resource_tests.cpp
  screen_tests.cpp
  search_tests.cpp
  shader_system_tests.cpp
  shader_tests.cpp
  system_tests.cpp
  ui_tests.cpp
  viewport_tests.cpp)
target_link_libraries(wlw_tests PRIVATE wlw::wlw wlw::kit wlw_doctest)
target_compile_definitions(wlw_tests PRIVATE WLW_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/data/LilitaOne-Regular.ttf")
doctest_discover_tests(wlw_tests)
