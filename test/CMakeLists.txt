enable_testing()

include_directories(
  . ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/fmtlib
  ${CMAKE_CURRENT_BINARY_DIR}/../src ${CMAKE_CURRENT_BINARY_DIR})

add_library(testdummy STATIC test_stubs.cc)
target_link_libraries(testdummy pcre::libpcre ncurses::libcurses)

add_executable(test_abbrev test_abbrev.cc)
target_link_libraries(test_abbrev diag pcre::libpcre)
add_test(NAME test_abbrev COMMAND test_abbrev)

add_executable(test_ansi_scrubber test_ansi_scrubber.cc)
target_link_libraries(test_ansi_scrubber diag)
add_test(NAME test_ansi_scrubber COMMAND test_ansi_scrubber)

add_executable(test_auto_fd test_auto_fd.cc)
target_link_libraries(test_auto_fd diag pcre::libpcre)
add_test(NAME test_auto_fd COMMAND test_auto_fd)

add_executable(test_auto_mem test_auto_mem.cc)
target_link_libraries(test_auto_mem diag pcre::libpcre)
add_test(NAME test_auto_mem COMMAND test_auto_mem)

add_executable(test_bookmarks test_bookmarks.cc)
target_link_libraries(test_bookmarks diag pcre::libpcre)
add_test(NAME test_bookmarks COMMAND test_bookmarks)

add_executable(test_date_time_scanner test_date_time_scanner.cc)
target_link_libraries(test_date_time_scanner diag pcre::libpcre)
add_test(NAME test_date_time_scanner COMMAND test_date_time_scanner)

add_executable(test_grep_proc2 test_grep_proc2.cc)
target_link_libraries(test_grep_proc2 diag pcre::libpcre)
add_test(NAME test_grep_proc2 COMMAND test_grep_proc2)

add_executable(test_line_buffer2 test_line_buffer2.cc)
target_link_libraries(test_line_buffer2 diag BZip2::BZip2 ZLIB::ZLIB)
add_test(NAME test_line_buffer2 COMMAND test_line_buffer2)

add_executable(test_log_accel test_log_accel.cc)
target_link_libraries(test_log_accel diag pcre::libpcre)
add_test(NAME test_log_accel COMMAND test_log_accel)

add_executable(lnav_doctests lnav_doctests.cc)
target_include_directories(lnav_doctests PUBLIC ../src/third-party/doctest-root)
target_link_libraries(lnav_doctests diag ${lnav_LIBS})
add_test(NAME lnav_doctests COMMAND lnav_doctests)

add_executable(test_pcrepp ../src/pcrepp/test_pcrepp.cc)
target_link_libraries(test_pcrepp diag pcre::libpcre)
add_test(NAME test_pcrepp COMMAND test_pcrepp)

add_executable(test_reltime test_reltime.cc)
target_include_directories(test_reltime PUBLIC ../src/third-party/doctest-root)
target_link_libraries(test_reltime diag pcre::libpcre)
add_test(NAME test_reltime COMMAND test_reltime)

add_executable(test_top_status test_top_status.cc)
target_link_libraries(test_top_status diag testdummy pcre::libpcre logfmt)
add_test(NAME test_top_status COMMAND test_top_status)

add_executable(drive_view_colors drive_view_colors.cc)
target_link_libraries(drive_view_colors diag testdummy ncurses::libcurses)

add_executable(drive_vt52_curses drive_vt52_curses.cc)
target_link_libraries(drive_vt52_curses diag ncurses::libcurses)

add_executable(drive_logfile drive_logfile.cc)
target_link_libraries(drive_logfile diag testdummy pcre::libpcre)

add_executable(drive_sql_anno drive_sql_anno.cc)
target_link_libraries(drive_sql_anno diag testdummy pcre::libpcre)

add_executable(drive_data_scanner drive_data_scanner.cc)
target_link_libraries(drive_data_scanner diag testdummy pcre::libpcre logfmt)

add_executable(scripty scripty.cc)
target_link_libraries(scripty diag ncurses::libcurses)
