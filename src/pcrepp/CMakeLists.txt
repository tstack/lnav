add_library(pcrepp STATIC ../config.h.in pcrepp.hh pcrepp.cc)

target_include_directories(pcrepp PUBLIC . .. ../fmtlib
                                         ${CMAKE_CURRENT_BINARY_DIR}/..)
target_link_libraries(pcrepp pcre::libpcre)
