cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

set(CMAKE_CXX_STANDARD 14)
project(
        lnav
        VERSION 0.11.1
        DESCRIPTION "An advanced log file viewer for the small-scale."
        HOMEPAGE_URL "https://lnav.org/"
        LANGUAGES CXX C
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

find_package(PkgConfig REQUIRED)

find_package(pcre2 QUIET)

if (NOT pcre2_FOUND)
    pkg_check_modules(pcre2 REQUIRED libpcre2-8 IMPORTED_TARGET)
    add_library(pcre2::pcre2 ALIAS PkgConfig::pcre2)
endif ()

find_package(readline QUIET)

if (NOT readline_FOUND)
    add_library(Readline INTERFACE)
    target_link_libraries(Readline INTERFACE
        /usr/lib64/libreadline.a
        /usr/lib64/libtinfo.a)
    add_library(readline::readline ALIAS Readline)
endif ()

find_package(ncurses QUIET)

if (NOT ncurses_FOUND)
    set(CURSES_NEED_NCURSES TRUE)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)
    add_library(Curses INTERFACE)
    target_include_directories(Curses INTERFACE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(Curses INTERFACE
        /usr/lib64/libncursesw.a
        /usr/lib64/libformw.a)
    add_library(ncurses::libcurses ALIAS Curses)
endif ()

find_package(SQLite3 REQUIRED)
find_package(BZip2 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

set(lnav_LIBS
        CURL::libcurl
        SQLite::SQLite3
        BZip2::BZip2
        ncurses::libcurses
        pcre2::pcre2
        readline::readline
        LibArchive::LibArchive
        ZLIB::ZLIB
        )

add_subdirectory(src)
# add_subdirectory(test)

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
