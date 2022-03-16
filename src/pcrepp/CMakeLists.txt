add_library(pcrepp STATIC ../config.h.in pcrepp.hh pcrepp.cc)

target_include_directories(pcrepp PUBLIC . .. ../fmtlib
                                         ${CMAKE_CURRENT_BINARY_DIR}/..)
target_link_libraries(pcrepp pcre::libpcre)

add_executable(test_pcrepp test_pcrepp.cc)
target_link_libraries(test_pcrepp pcrepp)
add_test(NAME test_pcrepp COMMAND test_pcrepp)
