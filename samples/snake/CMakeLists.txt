# Snake: the first game made with the engine, kept as a sample and a quick way to try engine
# changes. Run: build/<preset>/samples/snake/wlw_snake(.exe)
wlw_add_game(wlw_snake
  SOURCES
    src/main.cpp
    src/snake_app.cpp
    src/snake_app.h
    src/snake_game.cpp
    src/snake_game.h
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets")

set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT wlw_snake)

# The rules (SnakeGame) don't depend on the engine, so they're tested without a window (desktop only).
if(ANDROID)
  return()
endif()
add_executable(snake_game_tests tests/snake_game_tests.cpp src/snake_game.cpp)
target_compile_features(snake_game_tests PRIVATE cxx_std_20)
target_link_libraries(snake_game_tests PRIVATE wlw_doctest)
doctest_discover_tests(snake_game_tests TEST_PREFIX "snake: ")
