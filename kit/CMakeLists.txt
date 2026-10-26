# wlw::kit - game-level building blocks shared by games: save files, scores, level progress,
# daily streaks, ads (AdMob) and leaderboards (Play Games) through the native bridge (android/ holds
# the Kotlin side), the cloud-save interface. Built on the engine (wlw::wlw); the engine never uses it.
# Games link it next to the engine: target_link_libraries(<game> PRIVATE wlw::kit)
add_library(wlw_kit STATIC
  wlw/kit/ad_pacing.h
  wlw/kit/ads.h
  wlw/kit/ads.cpp
  wlw/kit/cloud_sync.h
  wlw/kit/daily.h
  wlw/kit/daily.cpp
  wlw/kit/kit_log.h
  wlw/kit/leaderboards.h
  wlw/kit/leaderboards.cpp
  wlw/kit/progress.h
  wlw/kit/progress.cpp
  wlw/kit/save_store.h
  wlw/kit/save_store.cpp)
add_library(wlw::kit ALIAS wlw_kit)

target_include_directories(wlw_kit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(wlw_kit PUBLIC wlw::wlw wlw_json)
target_compile_features(wlw_kit PUBLIC cxx_std_20)
set_target_properties(wlw_kit PROPERTIES POSITION_INDEPENDENT_CODE ON)
wlw_set_warnings(wlw_kit)
