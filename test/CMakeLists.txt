
enable_testing()

include_directories(
        .
        ${CMAKE_CURRENT_BINARY_DIR}/../src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(test_ansi_scrubber test_ansi_scrubber.cc)
target_link_libraries(test_ansi_scrubber diag)
add_test(NAME test_ansi_scrubber COMMAND test_ansi_scrubber)

add_executable(lnav_doctests lnav_doctests.cc)
target_link_libraries(lnav_doctests diag ${lnav_LIBS})
add_test(NAME lnav_doctests COMMAND lnav_doctests)

add_executable(test_pcrepp test_pcrepp.cc)
target_link_libraries(test_pcrepp diag PkgConfig::libpcre)
add_test(NAME test_pcrepp COMMAND test_pcrepp)

add_executable(test_line_buffer2 test_line_buffer2.cc)
target_link_libraries(test_line_buffer2
        diag
        BZip2::bz2
        ZLIB::zlib)
add_test(NAME test_line_buffer2 COMMAND test_line_buffer2)

add_executable(test_reltime test_reltime.cc)
target_link_libraries(test_reltime diag PkgConfig::libpcre)
add_test(NAME test_reltime COMMAND test_reltime)

add_executable(test_auto_mem test_auto_mem.cc)
target_link_libraries(test_auto_mem diag PkgConfig::libpcre)
add_test(NAME test_auto_mem COMMAND test_auto_mem)

add_executable(test_date_time_scanner test_date_time_scanner.cc)
target_link_libraries(test_date_time_scanner diag PkgConfig::libpcre)
add_test(NAME test_date_time_scanner COMMAND test_date_time_scanner)

add_executable(test_abbrev test_abbrev.cc)
target_link_libraries(test_abbrev diag PkgConfig::libpcre)
add_test(NAME test_abbrev COMMAND test_abbrev)

add_executable(drive_sql_anno drive_sql_anno.cc)
target_link_libraries(drive_sql_anno diag PkgConfig::libpcre)

add_executable(drive_data_scanner drive_data_scanner.cc)
target_link_libraries(drive_data_scanner diag PkgConfig::libpcre)

add_executable(scripty scripty.cc)
target_link_libraries(scripty diag PkgConfig::ncursesw)
