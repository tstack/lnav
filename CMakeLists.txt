cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

set(CMAKE_CXX_STANDARD 17)
project(
        lnav
        VERSION 0.12.3
        DESCRIPTION "An advanced log file viewer for the terminal."
        HOMEPAGE_URL "https://lnav.org/"
        LANGUAGES CXX C
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

find_package(SQLite3 REQUIRED)
find_package(BZip2 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(ZLIB REQUIRED)
find_package(pcre REQUIRED)
find_package(pcre2 REQUIRED)
find_package(readline REQUIRED)
find_package(ncurses REQUIRED)
find_package(CURL REQUIRED)

set(lnav_LIBS
        CURL::libcurl
        SQLite::SQLite3
        BZip2::BZip2
        ncurses::libcurses
        pcre::libpcre
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
