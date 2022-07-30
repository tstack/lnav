
add_library(
        logfmt
        STATIC
        logfmt.parser.hh
        logfmt.parser.cc
)

target_include_directories(
        logfmt
        PUBLIC
        .
        ${CMAKE_BINARY_DIR}/src
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(
        logfmt
        PRIVATE
        cppfmt
        cppscnlib
)

add_executable(
        logfmt.parser.test
        logfmt.parser.test.cc
)
target_include_directories(
        logfmt.parser.test
        PUBLIC
        .
        ${CMAKE_BINARY_DIR}/src
        ${CMAKE_SOURCE_DIR}/src
        ../../third-party/doctest-root)
target_link_libraries(
        logfmt.parser.test
        logfmt
        base
)

add_test(NAME logfmt.parser.test COMMAND logfmt.parser.test)
