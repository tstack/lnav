file(
    RELATIVE_PATH relative_path
    "/${lnav_INSTALL_CMAKEDIR}"
    "/${CMAKE_INSTALL_BINDIR}/${lnav_NAME}"
)

get_filename_component(prefix "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
set(config_dir "${prefix}/${lnav_INSTALL_CMAKEDIR}")
set(config_file "${config_dir}/lnavConfig.cmake")

message(STATUS "Installing: ${config_file}")
file(WRITE "${config_file}" "\
set(
    LNAV_EXECUTABLE
    \"\${CMAKE_CURRENT_LIST_DIR}/${relative_path}\"
    CACHE FILEPATH \"Path to the lnav executable\"
)
")
