
cmake_minimum_required(VERSION 3.2)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include("cmake/HunterGate.cmake")
HunterGate(
        URL "https://github.com/cpp-pm/hunter/archive/v0.23.269.tar.gz"
        SHA1 "64024b7b95b4c86d50ae05b926814448c93a70a0"
        LOCAL
)

set(CMAKE_CXX_STANDARD 14)
project(lnav
        VERSION 0.9.1)

include(CTest)
include(GNUInstallDirs)

hunter_add_package(libpcre)
find_package(libpcre CONFIG REQUIRED)
find_package(libpcrecpp CONFIG REQUIRED)

hunter_add_package(BZip2)
find_package(BZip2 CONFIG REQUIRED)

hunter_add_package(sqlite3)
find_package(sqlite3 CONFIG REQUIRED)

hunter_add_package(ZLIB)
find_package(ZLIB REQUIRED)

hunter_add_package(CURL)
find_package(CURL CONFIG REQUIRED)

hunter_add_package(ncursesw)
find_package(ncursesw CONFIG REQUIRED)

hunter_add_package(readline)
find_package(readline REQUIRED)

set(lnav_LIBS
        ZLIB::zlib
        BZip2::bz2
        sqlite3::sqlite3
        PkgConfig::libpcrecpp
        PkgConfig::libpcre
        CURL::libcurl
        readline::readline
        readline::history
        PkgConfig::ncursesw)

add_subdirectory(src)
add_subdirectory(test)
