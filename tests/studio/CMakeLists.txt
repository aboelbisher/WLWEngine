# The Studio's tests (studio/): built when the Studio is (WLW_BUILD_STUDIO: desktop with the debug UI).
# wlw_studio_tests: its logic without a window (devices, the Game panel's input forwarding).
add_executable(wlw_studio_tests studio_tests.cpp)
target_link_libraries(wlw_studio_tests PRIVATE wlw::studio wlw_doctest)
wlw_set_warnings(wlw_studio_tests)
doctest_discover_tests(wlw_studio_tests)

# wlw_studio_app_tests: a Studio on a hidden window playing a test game (Play / Pause / Step / Stop, a click in the
# Game panel). It needs a GL context: labelled gpu like wlw_app_tests, and its own binary for the same reason.
add_executable(wlw_studio_app_tests studio_app_tests.cpp)
target_link_libraries(wlw_studio_app_tests PRIVATE wlw::studio wlw_doctest)
wlw_set_warnings(wlw_studio_app_tests)
doctest_discover_tests(wlw_studio_app_tests TEST_PREFIX "gpu: " PROPERTIES LABELS gpu)
