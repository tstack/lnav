
cmake_minimum_required(VERSION 3.2)

include("cmake/HunterGate.cmake")
HunterGate(
        URL "https://github.com/ruslo/hunter/archive/v0.23.202.tar.gz"
        SHA1 "931b1cb6b36a7a96948d2cfc8ec5e553bf3160f3"
)

set(CMAKE_CXX_STANDARD 14)
project(lnav
        VERSION 0.8.6)

include(CTest)

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
        PkgConfig::libpcre
        PkgConfig::libpcrecpp
        CURL::libcurl
        readline::readline
        readline::history
        PkgConfig::ncursesw)

add_subdirectory(src)
add_subdirectory(test)
