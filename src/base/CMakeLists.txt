
add_library(base STATIC
        ../config.h

        auto_pid.cc
        date_time_scanner.cc
        humanize.cc
        humanize.network.cc
        humanize.time.cc
        intern_string.cc
        is_utf8.cc
        isc.cc
        lnav.gzip.cc
        lnav_log.cc
        network.tcp.cc
        paths.cc
        string_util.cc
        time_util.cc


        auto_pid.hh
        date_time_scanner.hh
        enum_util.hh
        func_util.hh
        future_util.hh
        humanize.hh
        humanize.network.hh
        humanize.time.hh
        injector.hh
        injector.bind.hh
        intern_string.hh
        is_utf8.hh
        isc.hh
        lrucache.hpp
        math_util.hh
        network.tcp.hh
        paths.hh
        result.h
        time_util.hh
        )

target_include_directories(
        base
        PUBLIC
        .
        ..
        ../fmtlib
        ${CMAKE_CURRENT_BINARY_DIR}/..
)
target_link_libraries(base cppfmt PkgConfig::libpcre)

add_executable(test_base

        humanize.file_size.tests.cc
        humanize.network.tests.cc
        humanize.time.tests.cc
        string_util.tests.cc
        network.tcp.tests.cc

        test_base.cc)
target_link_libraries(test_base base pcrepp)
add_test(NAME test_base COMMAND test_base)
