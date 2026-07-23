add_library(
        yajlpp STATIC
        ${CMAKE_BINARY_DIR}/src/config.h
        json_op.hh
        json_ptr.hh
        yajlpp.hh
        yajlpp_def.hh

        json_op.cc
        json_ptr.cc
        yajlpp.cc
)

target_include_directories(yajlpp PUBLIC . .. ../fmtlib
        ${CMAKE_CURRENT_BINARY_DIR}/..
        ../third-party
)
target_link_libraries(yajlpp
        base
        pcrepp
        yajl
        ${CURSES_LIBRARIES}
        datepp
)

add_executable(test_yajlpp test_yajlpp.cc)
target_link_libraries(test_yajlpp yajlpp)
add_test(NAME test_yajlpp COMMAND test_yajlpp)

add_executable(test_json_ptr test_json_ptr.cc)
target_link_libraries(test_json_ptr yajlpp)
add_test(NAME test_json_ptr COMMAND test_json_ptr)

add_executable(drive_json_op drive_json_op.cc)
target_link_libraries(drive_json_op yajlpp)

add_executable(drive_json_ptr_walk drive_json_ptr_walk.cc)
target_link_libraries(drive_json_ptr_walk yajlpp)
