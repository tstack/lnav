set_property(GLOBAL PROPERTY USE_FOLDERS YES)

# Call this function at the end of a directory scope to assign a folder to
# targets created in that directory. Utility targets will be assigned to the
# UtilityTargets folder, otherwise to the ${name}Targets folder. If a target
# already has a folder assigned, then that target will be skipped.
function(add_folders name)
  get_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_property(folder TARGET "${target}" PROPERTY FOLDER)
    if(DEFINED folder)
      continue()
    endif()
    set(folder Utility)
    get_property(type TARGET "${target}" PROPERTY TYPE)
    if(NOT type STREQUAL "UTILITY")
      set(folder "${name}")
    endif()
    set_property(TARGET "${target}" PROPERTY FOLDER "${folder}Targets")
  endforeach()
endfunction()
