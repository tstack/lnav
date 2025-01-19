cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

set(CMAKE_CXX_STANDARD 17)
project(
        lnav
        VERSION 0.12.4
        DESCRIPTION "An advanced log file viewer for the terminal."
        HOMEPAGE_URL "https://lnav.org/"
        LANGUAGES CXX C
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

set(CURSES_NEED_NCURSES TRUE)
find_package(PkgConfig REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(BZip2 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(ZLIB REQUIRED)
find_package(pcre2 CONFIG REQUIRED)
pkg_check_modules(readline REQUIRED IMPORTED_TARGET readline)
find_package(Curses REQUIRED)
find_package(CURL REQUIRED)

set(lnav_LIBS
        CURL::libcurl
        unofficial::sqlite3::sqlite3
        BZip2::BZip2
        ${CURSES_LIBRARIES}
        PCRE2::8BIT PCRE2::16BIT PCRE2::32BIT PCRE2::POSIX
        PkgConfig::readline
        LibArchive::LibArchive
        ZLIB::ZLIB
        ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/libunistring.a
)

add_subdirectory(src)
add_subdirectory(test)

# ---- Install rules ----

if (NOT CMAKE_SKIP_INSTALL_RULES)
    include(cmake/install-rules.cmake)
endif ()

# ---- Developer mode ----

if (NOT lnav_DEVELOPER_MODE)
    return()
elseif (NOT PROJECT_IS_TOP_LEVEL)
    message(
            AUTHOR_WARNING
            "Developer mode is intended for developers of lnav"
    )
endif ()

include(cmake/dev-mode.cmake)
