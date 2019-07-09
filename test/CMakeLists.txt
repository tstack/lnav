
enable_testing()

include_directories(
        ../../lbuild-debug/src
        ../src/
        /opt/local/include
        /usr/local/opt/sqlite3/include
        /usr/local/opt/pcre/include
        /usr/local/opt/readline/include
)

add_executable(lnav_doctests
        lnav_doctests.cc)
add_test(NAME lnav_doctests COMMAND lnav_doctests)

target_link_libraries(lnav_doctests diag ${lnav_LIBS})
add_executable(test_pcrepp test_pcrepp.cc ../src/base/lnav_log.cc ../src/pcrepp/pcrepp.cc)
add_test(NAME test_pcrepp COMMAND test_pcrepp)
add_executable(test_line_buffer2
        test_line_buffer2.cc
        ../src/base/lnav_log.cc
        ../src/base/is_utf8.cc
        ../src/fmtlib/format.cc
        ../src/fmtlib/posix.cc
        ../src/shared_buffer.cc
        ../src/line_buffer.hh
        ../src/line_buffer.cc)
target_link_libraries(test_line_buffer2
        BZip2::bz2
        ZLIB::zlib)
add_executable(test_reltime test_reltime.cc)
target_link_libraries(test_reltime diag)
add_executable(test_date_time_scanner test_date_time_scanner.cc)
target_link_libraries(test_date_time_scanner diag)
add_executable(test_abbrev test_abbrev.cc)
target_link_libraries(test_abbrev diag)
add_executable(drive_sql_anno drive_sql_anno.cc)
link_directories(/opt/local/lib)
target_link_libraries(test_pcrepp PkgConfig::libpcre)
target_link_libraries(test_reltime PkgConfig::libpcre)
target_link_libraries(lnav_doctests PkgConfig::libpcre)
target_link_libraries(test_date_time_scanner PkgConfig::libpcre)
target_link_libraries(test_abbrev PkgConfig::libpcre)
target_link_libraries(drive_sql_anno PkgConfig::libpcre diag)
