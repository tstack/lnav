add_library(remote STATIC ../config.h.in remote.ssh.cc remote.ssh.hh)

target_include_directories(remote PUBLIC . .. ../fmtlib
        ${CMAKE_CURRENT_BINARY_DIR}/..)
target_link_libraries(remote cppfmt)
