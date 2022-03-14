include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# find_package(<package>) call for consumers to find this project
set(package lnav)

install(
    TARGETS lnav
    RUNTIME COMPONENT lnav_Runtime
)

write_basic_package_version_file(
    "${package}ConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

# Allow package maintainers to freely override the path for the configs
set(
    lnav_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATADIR}/${package}"
    CACHE PATH "CMake package config location relative to the install prefix"
)
mark_as_advanced(lnav_INSTALL_CMAKEDIR)

install(
    FILES "${PROJECT_BINARY_DIR}/${package}ConfigVersion.cmake"
    DESTINATION "${lnav_INSTALL_CMAKEDIR}"
    COMPONENT lnav_Development
)

# Export variables for the install script to use
install(CODE "
set(lnav_NAME [[$<TARGET_FILE_NAME:lnav>]])
set(lnav_INSTALL_CMAKEDIR [[${lnav_INSTALL_CMAKEDIR}]])
set(CMAKE_INSTALL_BINDIR [[${CMAKE_INSTALL_BINDIR}]])
" COMPONENT lnav_Development)

install(
    SCRIPT cmake/install-script.cmake
    COMPONENT lnav_Development
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()
