enable_testing()

include_directories(
        . ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/fmtlib
        ${CMAKE_SOURCE_DIR}/src/third-party/date/include
        ${CMAKE_CURRENT_BINARY_DIR}/../src ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_abbrev test_abbrev.cc test_stubs.cc)
target_link_libraries(test_abbrev diag)
add_test(NAME test_abbrev COMMAND test_abbrev)

add_executable(test_ansi_scrubber test_ansi_scrubber.cc test_stubs.cc)
target_link_libraries(test_ansi_scrubber diag)
add_test(NAME test_ansi_scrubber COMMAND test_ansi_scrubber)

add_executable(test_auto_fd test_auto_fd.cc test_stubs.cc)
target_link_libraries(test_auto_fd diag)
add_test(NAME test_auto_fd COMMAND test_auto_fd)

add_executable(test_auto_mem test_auto_mem.cc test_stubs.cc)
target_link_libraries(test_auto_mem diag)
add_test(NAME test_auto_mem COMMAND test_auto_mem)

add_executable(test_column_namer test_column_namer.cc test_stubs.cc)
target_include_directories(test_column_namer PUBLIC ../src/third-party/doctest-root)
target_link_libraries(test_column_namer diag)
add_test(NAME test_column_namer COMMAND test_column_namer)

add_executable(document.sections.tests document.sections.tests.cc test_stubs.cc)
target_include_directories(document.sections.tests PUBLIC ../src/third-party/doctest-root)
target_link_libraries(document.sections.tests diag)
add_test(NAME document.sections.tests COMMAND document.sections.tests)

add_executable(test_bookmarks test_bookmarks.cc test_stubs.cc)
target_link_libraries(test_bookmarks diag)
add_test(NAME test_bookmarks COMMAND test_bookmarks)

add_executable(test_date_time_scanner test_date_time_scanner.cc)
target_include_directories(test_date_time_scanner PUBLIC ../src/third-party/doctest-root)
target_link_libraries(test_date_time_scanner base lnavdt diag)
add_test(NAME test_date_time_scanner COMMAND test_date_time_scanner)

add_executable(test_grep_proc2 test_grep_proc2.cc)
target_link_libraries(test_grep_proc2 lnavfileio)
add_test(NAME test_grep_proc2 COMMAND test_grep_proc2)

add_executable(test_line_buffer2 test_line_buffer2.cc)
target_link_libraries(test_line_buffer2 lnavfileio)
add_test(NAME test_line_buffer2 COMMAND test_line_buffer2)

add_executable(test_log_accel test_log_accel.cc test_stubs.cc)
target_link_libraries(test_log_accel diag)
add_test(NAME test_log_accel COMMAND test_log_accel)

add_executable(lnav_doctests lnav_doctests.cc test_stubs.cc)
target_include_directories(lnav_doctests PUBLIC ../src/third-party/doctest-root)
target_link_libraries(lnav_doctests diag ${lnav_LIBS})
add_test(NAME lnav_doctests COMMAND lnav_doctests)

add_executable(test_reltime test_reltime.cc test_stubs.cc)
target_include_directories(test_reltime PUBLIC ../src/third-party/doctest-root)
target_link_libraries(test_reltime diag)
add_test(NAME test_reltime COMMAND test_reltime)

add_executable(test_top_status test_top_status.cc test_stubs.cc)
target_link_libraries(test_top_status diag logfmt)
add_test(NAME test_top_status COMMAND test_top_status)

add_executable(test_text_anonymizer test_text_anonymizer.cc test_stubs.cc)
target_include_directories(test_text_anonymizer PUBLIC ../src/third-party/doctest-root)
target_link_libraries(test_text_anonymizer diag)
add_test(NAME test_text_anonymizer COMMAND test_text_anonymizer)

add_executable(drive_view_colors drive_view_colors.cc test_stubs.cc)
target_link_libraries(drive_view_colors diag)

add_executable(drive_listview drive_listview.cc test_stubs.cc)
target_link_libraries(drive_listview diag)

add_executable(drive_logfile drive_logfile.cc test_stubs.cc)
target_link_libraries(drive_logfile diag)

add_executable(drive_shlexer drive_shlexer.cc test_stubs.cc)
target_link_libraries(drive_shlexer diag)

add_executable(drive_sql_anno drive_sql_anno.cc test_stubs.cc)
target_link_libraries(drive_sql_anno diag)

add_executable(drive_textinput drive_textinput.cc test_stubs.cc)
target_link_libraries(drive_textinput diag)

add_executable(drive_data_scanner drive_data_scanner.cc test_stubs.cc)
target_link_libraries(drive_data_scanner diag logfmt)

add_executable(drive_doc_discovery drive_doc_discovery.cc test_stubs.cc)
target_link_libraries(drive_doc_discovery diag logfmt)

add_executable(scripty scripty.cc test_stubs.cc)
target_link_libraries(scripty diag)
