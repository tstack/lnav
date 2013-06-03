
TESTS_ENVIRONMENT = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT
LOG_COMPILER = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT

AM_CPPFLAGS = \
	-I$(top_srcdir)/src \
	$(SQLITE3_CFLAGS)

# AM_CFLAGS = -fprofile-arcs -ftest-coverage
# AM_CXXFLAGS = -fprofile-arcs -ftest-coverage

check_PROGRAMS = \
	drive_data_scanner \
	drive_line_buffer \
	drive_grep_proc \
	drive_listview \
	drive_logfile \
	drive_sequencer \
	drive_sql \
	drive_view_colors \
	drive_vt52_curses \
	drive_readline_curses \
	slicer \
	scripty \
	test_auto_fd \
	test_auto_mem \
	test_bookmarks \
	test_grep_proc2 \
	test_hist_source \
	test_line_buffer2 \
	test_pcrepp \
	test_top_status

AM_LDFLAGS = \
	$(SQLITE3_LDFLAGS)

LDADD = -lz

test_auto_fd_SOURCES = test_auto_fd.cc

test_auto_mem_SOURCES = test_auto_mem.cc

test_bookmarks_SOURCES = test_bookmarks.cc ../src/bookmarks.cc

test_grep_proc2_SOURCES = \
	../src/line_buffer.cc \
	../src/grep_proc.cc \
	test_grep_proc2.cc
test_grep_proc2_LDADD = $(PCRE_LIBS) -lz

test_hist_source_SOURCES = \
	../src/hist_source.cc \
	../src/view_curses.cc \
	test_hist_source.cc
test_hist_source_LDADD = $(CURSES_LIB) -lz

test_line_buffer2_SOURCES = \
	../src/line_buffer.cc \
	test_line_buffer2.cc

test_pcrepp_SOURCES = \
	test_pcrepp.cc

test_top_status_SOURCES = \
	../src/bookmarks.cc \
	../src/logfile.cc \
	../src/line_buffer.cc \
	../src/log_format.cc \
	../src/logfile_sub_source.cc \
	../src/listview_curses.cc \
	../src/textview_curses.cc \
	../src/data_scanner.cc \
	../src/data_parser.cc \
	../src/view_curses.cc \
	test_top_status.cc
test_top_status_LDADD = $(CURSES_LIB) -lz

drive_line_buffer_SOURCES = \
	../src/line_buffer.cc \
	drive_line_buffer.cc
drive_line_buffer_LDADD = $(CURSES_LIB) -lz

drive_grep_proc_SOURCES = \
	../src/line_buffer.cc \
	../src/grep_proc.cc \
	drive_grep_proc.cc
drive_grep_proc_LDADD = $(PCRE_LIBS) -lz

drive_listview_SOURCES = \
	../src/listview_curses.cc \
	../src/view_curses.cc \
	drive_listview.cc
drive_listview_LDADD = $(CURSES_LIB) -lz

drive_logfile_SOURCES = \
	../src/logfile.cc \
	../src/log_format.cc \
	../src/line_buffer.cc \
	drive_logfile.cc

drive_sequencer_SOURCES = \
	../src/grep_proc.cc \
	../src/logfile.cc \
	../src/log_format.cc \
	../src/line_buffer.cc \
	../src/sequence_matcher.cc \
	drive_sequencer.cc
drive_sequencer_LDADD = -lcrypto

drive_data_scanner_SOURCES = \
	drive_data_scanner.cc
drive_data_scanner_LDADD = ../src/libdiag.a -lcrypto

drive_view_colors_SOURCES = \
	../src/view_curses.cc \
	drive_view_colors.cc
drive_view_colors_LDADD = $(CURSES_LIB)

drive_vt52_curses_SOURCES = \
	../src/vt52_curses.cc \
	../src/view_curses.cc \
	drive_vt52_curses.cc
drive_vt52_curses_LDADD = $(CURSES_LIB)

drive_readline_curses_SOURCES = \
	../src/vt52_curses.cc \
	../src/readline_curses.cc \
	../src/view_curses.cc \
	drive_readline_curses.cc
drive_readline_curses_LDADD = $(CURSES_LIB) $(READLINE_LIBS)

drive_sql_SOURCES = \
	drive_sql.cc
drive_sql_LDADD = ../src/libdiag.a $(SQLITE3_LIBS)

slicer_SOURCES = \
	../src/line_buffer.cc \
	slicer.cc

scripty_SOURCES = scripty.cc

dist_noinst_SCRIPTS = \
	test_data_parser.sh \
	test_grep_proc.sh \
	test_line_buffer.sh \
	test_listview.sh \
	test_logfile.sh \
	test_sql_fs_func.sh \
	test_view_colors.sh \
	test_vt52_curses.sh

dist_noinst_DATA = \
	datafile_simple.0 \
	datafile_simple.1 \
	datafile_simple.2 \
	datafile_simple.3 \
	datafile_simple.4 \
	datafile_simple.5 \
	datafile_simple.6 \
	datafile_simple.7 \
	listview_output.0 \
	listview_output.1 \
	listview_output.2 \
	listview_output.3 \
	listview_output.4 \
	listview_output.5 \
	listview_output.6 \
	logfile_access_log.0 \
	logfile_empty.0 \
	logfile_generic.0 \
	logfile_glog.0 \
	logfile_strace_log.0 \
	logfile_syslog.0 \
	logfile_syslog.1 \
	logfile_tcsh_history.0 \
	logfile_with_a_really_long_name_to_test_a_bug_with_long_names.0 \
	view_colors_output.0 \
	vt52_curses_input.0 \
	vt52_curses_input.1 \
	vt52_curses_output.0 \
	vt52_curses_output.1 \
	log-samples/sample-27353a72ba4025448f261dcfa6ea16e474187795.txt \
	log-samples/sample-70c906b3c1a1cf03f15bde92ee78edfa6f9b7960.txt \
	log-samples/sample-ad31f12d2adabd07e3ddda3ad5b0dbf6b49c4c99.txt

TESTS = test_bookmarks \
	test_auto_fd \
	test_auto_mem \
	test_line_buffer.sh \
	test_line_buffer2 \
	test_grep_proc.sh \
	test_grep_proc2 \
	test_hist_source \
	test_listview.sh \
	test_pcrepp \
	test_sql_fs_func.sh \
	test_view_colors.sh \
	test_vt52_curses.sh \
	test_top_status \
	test_data_parser.sh

DISTCLEANFILES = \
	*.dat \
	*.diff \
	*.index \
	*.tmp
