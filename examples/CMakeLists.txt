# Sample games used to exercise the engine. Assets are shared in examples/assets/.
set(WLW_EXAMPLE_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_executable(wlw_example_arena
  arena/main.cpp
  arena/game.cpp
  arena/game.h
  arena/level.cpp
  arena/level.h
  arena/player_controller.h)
target_link_libraries(wlw_example_arena PRIVATE wlw::wlw)
wlw_set_warnings(wlw_example_arena)
wlw_add_assets(wlw_example_arena "${WLW_EXAMPLE_ASSETS}")

add_executable(wlw_example_platformer
  platformer/main.cpp
  platformer/platformer_game.cpp
  platformer/platformer_game.h
  platformer/platformer_level.cpp
  platformer/platformer_level.h
  platformer/platformer_player_controller.h)
target_link_libraries(wlw_example_platformer PRIVATE wlw::wlw)
wlw_set_warnings(wlw_example_platformer)
wlw_add_assets(wlw_example_platformer "${WLW_EXAMPLE_ASSETS}")

set_target_properties(wlw_example_arena wlw_example_platformer PROPERTIES FOLDER "examples")
