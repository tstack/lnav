
cmake_minimum_required (VERSION 2.6)
SET(CMAKE_CXX_STANDARD 14)
project (lnav)


set(lnav_LIBS
        /usr/lib/libz.dylib
        /usr/lib/libbz2.dylib
        /usr/local/opt/sqlite/lib/libsqlite3.a
        /usr/local/opt/pcre/lib/libpcre.a
        /usr/local/opt/pcre/lib/libpcrecpp.a
        /usr/local/opt/readline/lib/libreadline.a
        /usr/local/opt/ncurses/lib/libncurses.a)

add_subdirectory(src)
add_subdirectory(test)
