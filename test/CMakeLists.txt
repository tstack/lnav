
enable_testing()

include_directories(
        .
        ${CMAKE_CURRENT_BINARY_DIR}/../src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(test_abbrev test_abbrev.cc)
target_link_libraries(test_abbrev diag PkgConfig::libpcre)
add_test(NAME test_abbrev COMMAND test_abbrev)

add_executable(test_ansi_scrubber test_ansi_scrubber.cc)
target_link_libraries(test_ansi_scrubber diag)
add_test(NAME test_ansi_scrubber COMMAND test_ansi_scrubber)

add_executable(test_auto_fd test_auto_fd.cc)
target_link_libraries(test_auto_fd diag PkgConfig::libpcre)
add_test(NAME test_auto_fd COMMAND test_auto_fd)

add_executable(test_auto_mem test_auto_mem.cc)
target_link_libraries(test_auto_mem diag PkgConfig::libpcre)
add_test(NAME test_auto_mem COMMAND test_auto_mem)

add_executable(test_bookmarks test_bookmarks.cc)
target_link_libraries(test_bookmarks diag PkgConfig::libpcre)
add_test(NAME test_bookmarks COMMAND test_bookmarks)

add_executable(test_date_time_scanner test_date_time_scanner.cc)
target_link_libraries(test_date_time_scanner diag PkgConfig::libpcre)
add_test(NAME test_date_time_scanner COMMAND test_date_time_scanner)

add_executable(test_grep_proc2 test_grep_proc2.cc)
target_link_libraries(test_grep_proc2 diag PkgConfig::libpcre)
add_test(NAME test_grep_proc2 COMMAND test_grep_proc2)

add_executable(test_line_buffer2 test_line_buffer2.cc)
target_link_libraries(test_line_buffer2
        diag
        BZip2::bz2
        ZLIB::zlib)
add_test(NAME test_line_buffer2 COMMAND test_line_buffer2)

add_executable(test_log_accel test_log_accel.cc)
target_link_libraries(test_log_accel diag PkgConfig::libpcre)
add_test(NAME test_log_accel COMMAND test_log_accel)

add_executable(lnav_doctests lnav_doctests.cc)
target_link_libraries(lnav_doctests diag ${lnav_LIBS})
add_test(NAME lnav_doctests COMMAND lnav_doctests)

add_executable(test_pcrepp ../src/pcrepp/test_pcrepp.cc)
target_link_libraries(test_pcrepp diag PkgConfig::libpcre)
add_test(NAME test_pcrepp COMMAND test_pcrepp)

add_executable(test_reltime test_reltime.cc)
target_link_libraries(test_reltime diag PkgConfig::libpcre)
add_test(NAME test_reltime COMMAND test_reltime)

add_executable(test_top_status test_top_status.cc)
target_link_libraries(test_top_status diag PkgConfig::libpcre)
add_test(NAME test_top_status COMMAND test_top_status)

add_executable(drive_vt52_curses drive_vt52_curses.cc)
target_link_libraries(drive_vt52_curses diag PkgConfig::ncursesw)

add_executable(drive_sql_anno drive_sql_anno.cc)
target_link_libraries(drive_sql_anno diag PkgConfig::libpcre)

add_executable(drive_data_scanner drive_data_scanner.cc)
target_link_libraries(drive_data_scanner diag PkgConfig::libpcre)

add_executable(scripty scripty.cc)
target_link_libraries(scripty diag PkgConfig::ncursesw)
