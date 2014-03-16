
TESTS_ENVIRONMENT = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT
LOG_COMPILER = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT

simple-db.db: simple-db.sql
	rm -f $@
	$(SQLITE3_CMD) $@ < $(srcdir)/simple-db.sql

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
	drive_mvwattrline \
	drive_sequencer \
	drive_sql \
	drive_view_colors \
	drive_vt52_curses \
	drive_readline_curses \
	slicer \
	scripty \
	test_ansi_scrubber \
	test_auto_fd \
	test_auto_mem \
	test_bookmarks \
	test_grep_proc2 \
	test_hist_source \
	test_line_buffer2 \
	test_log_accel \
	test_pcrepp \
	test_top_status \
	test_yajlpp

AM_LDFLAGS = \
	$(STATIC_LDFLAGS) \
	$(SQLITE3_LDFLAGS) \
	-pthread

LDADD = -lz

test_ansi_scrubber_SOURCES = test_ansi_scrubber.cc
test_ansi_scrubber_LDADD = ../src/libdiag.a $(CURSES_LIB)

test_auto_fd_SOURCES = test_auto_fd.cc
test_auto_fd_LDADD = ../src/libdiag.a

test_auto_mem_SOURCES = test_auto_mem.cc

test_bookmarks_SOURCES = test_bookmarks.cc
test_bookmarks_LDADD = ../src/libdiag.a

test_grep_proc2_SOURCES = test_grep_proc2.cc
test_grep_proc2_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz

test_hist_source_SOURCES = test_hist_source.cc
test_hist_source_LDADD = ../src/libdiag.a $(CURSES_LIB) -lz

test_line_buffer2_SOURCES = test_line_buffer2.cc
test_line_buffer2_LDADD = ../src/libdiag.a

test_log_accel_SOURCES = test_log_accel.cc
test_log_accel_LDADD = ../src/libdiag.a

test_pcrepp_SOURCES = test_pcrepp.cc
test_pcrepp_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz

test_top_status_SOURCES = test_top_status.cc
test_top_status_LDADD = ../src/libdiag.a $(CURSES_LIB) $(PCRE_LIBS) $(SQLITE3_LIBS) -lz

test_yajlpp_SOURCES = test_yajlpp.cc
test_yajlpp_LDADD = ../src/libdiag.a

drive_line_buffer_SOURCES = drive_line_buffer.cc
drive_line_buffer_LDADD = ../src/libdiag.a $(CURSES_LIB) -lz

drive_grep_proc_SOURCES = drive_grep_proc.cc
drive_grep_proc_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz

drive_listview_SOURCES = drive_listview.cc
drive_listview_LDADD = ../src/libdiag.a $(CURSES_LIB) -lz

drive_logfile_SOURCES = drive_logfile.cc
drive_logfile_LDADD = \
	../src/libdiag.a \
	../src/default-log-formats-json.o \
	../src/dump-pid-sh.o \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	$(PCRE_LIBS) \
	-lpcrecpp

drive_sequencer_SOURCES = drive_sequencer.cc
drive_sequencer_LDADD = ../src/libdiag.a $(CURSES_LIB) $(SQLITE3_LIBS)

drive_data_scanner_SOURCES = \
	drive_data_scanner.cc
drive_data_scanner_LDADD = \
	../src/libdiag.a \
	../src/default-log-formats-json.o \
	../src/dump-pid-sh.o \
	$(PCRE_LIBS) \
	$(SQLITE3_LIBS) \
	-lpcrecpp \
	$(CURSES_LIB)

drive_mvwattrline_SOURCES = drive_mvwattrline.cc
drive_mvwattrline_LDADD = ../src/libdiag.a $(CURSES_LIB)

drive_view_colors_SOURCES = drive_view_colors.cc
drive_view_colors_LDADD = ../src/libdiag.a $(CURSES_LIB)

drive_vt52_curses_SOURCES = drive_vt52_curses.cc
drive_vt52_curses_LDADD = ../src/libdiag.a $(CURSES_LIB)

drive_readline_curses_SOURCES = drive_readline_curses.cc
drive_readline_curses_LDADD = ../src/libdiag.a $(READLINE_LIBS) $(CURSES_LIB)

drive_sql_SOURCES = \
	drive_sql.cc
drive_sql_LDADD = \
	../src/libdiag.a \
	$(SQLITE3_LIBS) \
	$(PCRE_LIBS) \
	$(CURSES_LIB) \
	$(READLINE_LIBS) \
	-lpcrecpp

slicer_SOURCES = slicer.cc
slicer_LDADD = ../src/libdiag.a

scripty_SOURCES = scripty.cc
scripty_LDADD = ../src/libdiag.a

dist_noinst_SCRIPTS = \
	parser_debugger.py \
	test_cmds.sh \
	test_data_parser.sh \
	test_grep_proc.sh \
	test_json_format.sh \
	test_line_buffer.sh \
	test_listview.sh \
	test_logfile.sh \
	test_mvwattrline.sh \
	test_sessions.sh \
	test_sql.sh \
	test_sql_coll_func.sh \
	test_sql_str_func.sh \
	test_sql_fs_func.sh \
	test_view_colors.sh \
	test_vt52_curses.sh

dist_noinst_DATA = \
	ansi-colors.0.in \
	datafile_simple.0 \
	datafile_simple.1 \
	datafile_simple.2 \
	datafile_simple.3 \
	datafile_simple.4 \
	datafile_simple.5 \
	datafile_simple.6 \
	datafile_simple.7 \
	datafile_simple.8 \
	datafile_simple.9 \
	datafile_simple.10 \
	datafile_simple.11 \
	listview_output.0 \
	listview_output.1 \
	listview_output.2 \
	listview_output.3 \
	listview_output.4 \
	listview_output.5 \
	listview_output.6 \
	logfile_access_log.0 \
	logfile_access_log.1 \
	logfile_empty.0 \
	logfile_generic.0 \
	logfile_glog.0 \
	logfile_json.json \
	logfile_multiline.0 \
	logfile_strace_log.0 \
	logfile_syslog.0 \
	logfile_syslog.1 \
	logfile_tcsh_history.0 \
	logfile_with_a_really_long_name_to_test_a_bug_with_long_names.0 \
	mvwattrline_output.0 \
	simple-db.sql \
	view_colors_output.0 \
	vt52_curses_input.0 \
	vt52_curses_input.1 \
	vt52_curses_output.0 \
	vt52_curses_output.1 \
	formats/jsontest/format.json \
	log-samples/sample-27353a72ba4025448f261dcfa6ea16e474187795.txt \
	log-samples/sample-70c906b3c1a1cf03f15bde92ee78edfa6f9b7960.txt \
	log-samples/sample-ad31f12d2adabd07e3ddda3ad5b0dbf6b49c4c99.txt

nodist_noinst_DATA = \
	simple-db.db

TESTS = \
	test_ansi_scrubber \
	test_auto_fd \
	test_auto_mem \
	test_bookmarks \
	test_cmds.sh \
	test_line_buffer2 \
	test_line_buffer.sh \
	test_listview.sh \
	test_mvwattrline.sh \
	test_grep_proc.sh \
	test_grep_proc2 \
	test_hist_source \
	test_json_format.sh \
	test_log_accel \
	test_logfile.sh \
	test_pcrepp \
	test_sessions.sh \
	test_sql.sh \
	test_sql_coll_func.sh \
	test_sql_fs_func.sh \
	test_sql_str_func.sh \
	test_view_colors.sh \
	test_vt52_curses.sh \
	test_top_status \
	test_data_parser.sh \
	test_yajlpp

XFAIL_TESTS = \
	test_listview.sh

DISTCLEANFILES = \
	*.dat \
	*.err \
	*.db \
	*.dpt \
	*.diff \
	*.index \
	*.tmp \
	*.gz \
	*.bz2

distclean-local:
	rm -rf sessions
