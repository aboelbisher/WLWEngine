# Unit tests for engine code that runs without a window or GPU (math, input, animation, particles, meshes).
# Built only when the engine is the top-level project. Run: ctest --test-dir build/<preset>
function(wlw_add_engine_test name)
  add_executable(${name} ${name}.cpp check.h)
  target_link_libraries(${name} PRIVATE wlw::wlw)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

wlw_add_engine_test(anim_tests)
wlw_add_engine_test(fx_tests)
wlw_add_engine_test(input_tests)
wlw_add_engine_test(picking_tests)
wlw_add_engine_test(primitives_tests)
