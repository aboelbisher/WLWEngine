# Golden-image tests: scenes drawn offscreen in a hidden window and compared with the PNGs in
# references/ (see golden.h). Run: ctest --test-dir build/<preset> -L gpu, or wlw_gpu_tests [--update].
add_executable(wlw_gpu_tests
  gpu_test_main.cpp
  golden.cpp
  golden.h
  scene_tests.cpp
  shader_compile_tests.cpp)
target_link_libraries(wlw_gpu_tests PRIVATE wlw::wlw wlw_doctest wlw_test_support)
target_compile_definitions(wlw_gpu_tests PRIVATE
  WLW_GPU_REFERENCES="${CMAKE_CURRENT_SOURCE_DIR}/references"
  WLW_GPU_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/gpu_output"
  WLW_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../data/LilitaOne-Regular.ttf")
wlw_set_warnings(wlw_gpu_tests)
doctest_discover_tests(wlw_gpu_tests TEST_PREFIX "gpu: " PROPERTIES LABELS gpu)

# The Application's frame loop on a hidden window (app_tests.cpp). Its own binary: GL objects belong to the
# context that made them, and an app's shader warm-up prepares every resource alive in the process, so an app
# next to the golden tests' context would compile their shaders into its own context and take them along.
add_executable(wlw_app_tests app_tests.cpp)
target_link_libraries(wlw_app_tests PRIVATE wlw::wlw wlw_doctest)
wlw_set_warnings(wlw_app_tests)
doctest_discover_tests(wlw_app_tests TEST_PREFIX "gpu: " PROPERTIES LABELS gpu)
