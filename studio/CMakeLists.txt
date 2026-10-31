# wlw::studio - the Studio, the engine's editor (ROADMAP D12, D20): a desktop-only library on the engine and its
# debug UI's Dear ImGui (docking). The runtime never includes it. Run by wlw_studio (below: engine types only) and by
# each game's <game>_studio (wlw_add_game(... STUDIO ...) in cmake/WLWHelpers.cmake: the game's code built in).
add_library(wlw_studio_lib STATIC
  wlw/studio/devices.cpp
  wlw/studio/devices.h
  wlw/studio/game_input.cpp
  wlw/studio/game_input.h
  wlw/studio/game_panel.cpp
  wlw/studio/game_panel.h
  wlw/studio/play_session.cpp
  wlw/studio/play_session.h
  wlw/studio/studio.cpp
  wlw/studio/studio.h
  wlw/studio/studio_log.h)
add_library(wlw::studio ALIAS wlw_studio_lib)

target_include_directories(wlw_studio_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wlw_studio_lib PUBLIC wlw::wlw)
target_compile_features(wlw_studio_lib PUBLIC cxx_std_20)
set_target_properties(wlw_studio_lib PROPERTIES FOLDER "studio")
wlw_set_warnings(wlw_studio_lib)

add_executable(wlw_studio main/wlw_studio_main.cpp)
target_link_libraries(wlw_studio PRIVATE wlw::studio)
set_target_properties(wlw_studio PROPERTIES FOLDER "studio")
wlw_set_warnings(wlw_studio)
