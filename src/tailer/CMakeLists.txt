
add_library(
        tailercommon

        sha-256.c
        sha-256.h

        tailer.c
        tailer.h
)

add_executable(
        tailer

        tailer.main.c
)

target_link_libraries(tailer tailercommon)

add_library(
        tailerpp

        tailerpp.hh
        tailerpp.cc
)
target_link_libraries(tailerpp base)

add_executable(
        drive_tailer

        drive_tailer.cc
)

target_include_directories(
        drive_tailer
        PUBLIC
        .
        ..
        ../fmtlib
        ${CMAKE_CURRENT_BINARY_DIR}/..
)
target_link_libraries(drive_tailer base tailercommon tailerpp)
