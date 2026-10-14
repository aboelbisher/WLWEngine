# Golden-image tests: scenes drawn offscreen in a hidden window and compared with the PNGs in
# references/ (see golden.h). Run: ctest --test-dir build/<preset> -L gpu, or wlw_gpu_tests [--update].
add_executable(wlw_gpu_tests
  gpu_test_main.cpp
  golden.cpp
  golden.h
  scene_tests.cpp)
target_link_libraries(wlw_gpu_tests PRIVATE wlw::wlw wlw_doctest wlw_test_support)
target_compile_definitions(wlw_gpu_tests PRIVATE
  WLW_GPU_REFERENCES="${CMAKE_CURRENT_SOURCE_DIR}/references"
  WLW_GPU_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/gpu_output"
  WLW_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../data/LilitaOne-Regular.ttf")
wlw_set_warnings(wlw_gpu_tests)
doctest_discover_tests(wlw_gpu_tests TEST_PREFIX "gpu: " PROPERTIES LABELS gpu)
