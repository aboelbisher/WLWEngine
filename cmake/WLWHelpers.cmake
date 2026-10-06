# Helpers available to the engine and to any game that does add_subdirectory(<engine>).

# wlw_add_assets(<target> <assets_dir>)
# Copies the contents of <assets_dir> next to the target's executable after every build,
# and makes IDE debuggers start in that folder, so relative asset paths work everywhere.
function(wlw_add_assets target assets_dir)
  get_filename_component(assets_dir "${assets_dir}" ABSOLUTE)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(copy_cmd copy_directory_if_different)
  else()
    set(copy_cmd copy_directory)
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E ${copy_cmd} "${assets_dir}" "$<TARGET_FILE_DIR:${target}>"
    COMMENT "Copying assets for ${target}"
    VERBATIM)
  set_target_properties(${target} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
    XCODE_SCHEME_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>")
endfunction()

# wlw_set_warnings(<target>) - common compiler settings for engine + game targets.
function(wlw_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3 /utf-8 /MP)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endfunction()
