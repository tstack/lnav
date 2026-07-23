add_library(remote STATIC ${CMAKE_BINARY_DIR}/src/config.h remote.ssh.cc remote.ssh.hh)

target_include_directories(remote PUBLIC . .. ../fmtlib
        ${CMAKE_CURRENT_BINARY_DIR}/..)
target_link_libraries(remote cppfmt)
