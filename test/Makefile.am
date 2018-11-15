
TESTS_ENVIRONMENT = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT
LOG_COMPILER = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT

RM_V = $(RM_V_@AM_V@)
RM_V_ = $(RM_V_@AM_DEFAULT_V@)
RM_V_0 = @echo "  RM      " $@;

AM_CPPFLAGS = \
	-Wall \
	-I$(top_srcdir)/src \
	$(READLINE_CFLAGS) \
	$(SQLITE3_CFLAGS)

# AM_CFLAGS = -fprofile-arcs -ftest-coverage
# AM_CXXFLAGS = -fprofile-arcs -ftest-coverage

check_PROGRAMS = \
	drive_data_scanner \
	drive_line_buffer \
	drive_grep_proc \
	drive_json_op \
	drive_json_ptr_walk \
	drive_listview \
	drive_logfile \
	drive_mvwattrline \
	drive_sequencer \
	drive_shlexer \
	drive_sql \
	drive_sql_anno \
	drive_view_colors \
	drive_vt52_curses \
	drive_readline_curses \
	lnav_doctests \
	slicer \
	scripty \
	test_abbrev \
	test_ansi_scrubber \
	test_auto_fd \
	test_auto_mem \
	test_bookmarks \
	test_date_time_scanner \
	test_grep_proc2 \
	test_json_ptr \
	test_line_buffer2 \
	test_log_accel \
	test_ncurses_unicode \
	test_pcrepp \
	test_reltime \
	test_top_status \
	test_yajlpp

AM_LDFLAGS = \
	$(STATIC_LDFLAGS) \
	$(SQLITE3_LDFLAGS) \
	$(READLINE_LDFLAGS) \
	$(CURSES_LIB) \
	-pthread

CONFIG_OBJS = \
	../src/default-config-json.o \
	../src/keymap-default.o

TEXT2C_OBJS = \
	../src/dhclient-summary.o \
	../src/dump-pid-sh.o \
	../src/lnav-pop-view.o \
	../src/partition-by-boot.o \
	../src/search-for.o

LDADD = -lz $(CURSES_LIB)

test_ansi_scrubber_SOURCES = test_ansi_scrubber.cc
test_ansi_scrubber_LDADD = ../src/libdiag.a $(CURSES_LIB) \
	$(CONFIG_OBJS)

test_auto_fd_SOURCES = test_auto_fd.cc
test_auto_fd_LDADD = ../src/libdiag.a

test_auto_mem_SOURCES = test_auto_mem.cc

test_bookmarks_SOURCES = test_bookmarks.cc
test_bookmarks_LDADD = ../src/libdiag.a

test_date_time_scanner_SOURCES = test_date_time_scanner.cc
test_date_time_scanner_LDADD = ../src/libdiag.a $(SQLITE3_LIBS)

test_grep_proc2_SOURCES = test_grep_proc2.cc
test_grep_proc2_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz

test_line_buffer2_SOURCES = test_line_buffer2.cc
test_line_buffer2_LDADD = ../src/libdiag.a

test_log_accel_SOURCES = test_log_accel.cc
test_log_accel_LDADD = ../src/libdiag.a

test_pcrepp_SOURCES = test_pcrepp.cc
test_pcrepp_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz

test_top_status_SOURCES = test_top_status.cc
test_top_status_LDADD = \
	../src/libdiag.a \
	$(CONFIG_OBJS) \
	$(CURSES_LIB) \
	$(PCRE_LIBS) \
	$(SQLITE3_LIBS) \
	$(LIBCURL) \
	-lz

test_yajlpp_SOURCES = test_yajlpp.cc
test_yajlpp_LDADD = ../src/libdiag.a

test_abbrev_SOURCES = test_abbrev.cc
test_abbrev_LDADD = ../src/libdiag.a

test_json_ptr_SOURCES = test_json_ptr.cc
test_json_ptr_LDADD = ../src/libdiag.a

test_reltime_SOURCES = test_reltime.cc
test_reltime_LDADD = ../src/libdiag.a

test_ncurses_unicode_SOURCES = test_ncurses_unicode.cc

lnav_doctests_SOURCES = lnav_doctests.cc
lnav_doctests_LDADD = ../src/libdiag.a $(CONFIG_OBJS) $(CURSES_LIB)

drive_line_buffer_SOURCES = drive_line_buffer.cc
drive_line_buffer_LDADD = ../src/libdiag.a $(CURSES_LIB) -lz $(CURSES_LIB)

drive_grep_proc_SOURCES = drive_grep_proc.cc
drive_grep_proc_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz $(CURSES_LIB)

drive_json_op_SOURCES = drive_json_op.cc
drive_json_op_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz $(CURSES_LIB)

drive_json_ptr_walk_SOURCES = drive_json_ptr_walk.cc
drive_json_ptr_walk_LDADD = ../src/libdiag.a $(PCRE_LIBS) -lz $(CURSES_LIB)

drive_listview_SOURCES = drive_listview.cc
drive_listview_LDADD = ../src/libdiag.a $(CURSES_LIB) -lz $(CURSES_LIB) \
	$(CONFIG_OBJS)

drive_logfile_SOURCES = drive_logfile.cc
drive_logfile_LDADD = \
	../src/libdiag.a \
	../src/default-log-formats-json.o \
	$(CONFIG_OBJS) \
	$(TEXT2C_OBJS) \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	$(PCRE_LIBS) \
	-lpcrecpp

drive_sequencer_SOURCES = drive_sequencer.cc
drive_sequencer_LDADD = ../src/libdiag.a $(CURSES_LIB) $(SQLITE3_LIBS)

drive_shlexer_SOURCES = drive_shlexer.cc
drive_shlexer_LDADD = ../src/libdiag.a

drive_data_scanner_SOURCES = \
	drive_data_scanner.cc
drive_data_scanner_LDADD = \
	../src/libdiag.a \
	../src/default-log-formats-json.o \
	$(CONFIG_OBJS) \
	$(TEXT2C_OBJS) \
	$(LIBCURL) \
	$(PCRE_LIBS) \
	$(SQLITE3_LIBS) \
	-lpcrecpp \
	$(CURSES_LIB)

drive_mvwattrline_SOURCES = drive_mvwattrline.cc
drive_mvwattrline_LDADD = ../src/libdiag.a $(CURSES_LIB) \
	$(CONFIG_OBJS)

drive_view_colors_SOURCES = drive_view_colors.cc
drive_view_colors_LDADD = ../src/libdiag.a $(CURSES_LIB) \
	$(CONFIG_OBJS)

drive_vt52_curses_SOURCES = drive_vt52_curses.cc
drive_vt52_curses_LDADD = ../src/libdiag.a $(CURSES_LIB) $(CONFIG_OBJS)

drive_readline_curses_SOURCES = drive_readline_curses.cc
drive_readline_curses_LDADD = ../src/libdiag.a $(READLINE_LIBS) $(CURSES_LIB) \
	$(CONFIG_OBJS)

drive_sql_SOURCES = \
	drive_sql.cc
drive_sql_LDADD = \
	../src/libdiag.a \
	$(CONFIG_OBJS) \
	$(SQLITE3_LIBS) \
	$(PCRE_LIBS) \
	$(CURSES_LIB) \
	$(READLINE_LIBS) \
	$(LIBCURL) \
	$(TEXT2C_OBJS) \
	-lpcrecpp

drive_sql_anno_SOURCES = \
	drive_sql_anno.cc
drive_sql_anno_LDADD = \
	../src/libdiag.a \
	$(CONFIG_OBJS) \
	$(SQLITE3_LIBS) \
	$(PCRE_LIBS) \
	$(CURSES_LIB) \
	$(READLINE_LIBS) \
	$(LIBCURL) \
	-lpcrecpp

slicer_SOURCES = slicer.cc
slicer_LDADD = ../src/libdiag.a $(CURSES_LIB)

scripty_SOURCES = scripty.cc
scripty_LDADD = ../src/libdiag.a $(CURSES_LIB)

dist_noinst_SCRIPTS = \
	parser_debugger.py \
	test_cli.sh \
	test_cmds.sh \
	test_curl.sh \
	test_data_parser.sh \
	test_format_installer.sh \
	test_format_loader.sh \
	test_grep_proc.sh \
	test_json_format.sh \
	test_json_op.sh \
	test_json_ptr_walk.sh \
	test_line_buffer.sh \
	test_listview.sh \
	test_logfile.sh \
	test_meta.sh \
	test_mvwattrline.sh \
	test_scripts.sh \
	test_sessions.sh \
	test_shlexer.sh \
	test_sql.sh \
	test_sql_coll_func.sh \
	test_sql_json_func.sh \
	test_sql_str_func.sh \
	test_sql_time_func.sh \
	test_sql_fs_func.sh \
	test_view_colors.sh \
	test_vt52_curses.sh \
	test_pretty_print.sh

dist_noinst_DATA = \
	ansi-colors.0.in \
	bad-config/formats/invalid-regex/format.json \
	bad-config/formats/invalid-sample/format.json \
	bad-config/formats/invalid-sql/init.sql \
	bad-config/formats/no-samples/format.json \
	bad-config-json/formats/invalid-json/format.json \
	bad-config-json/formats/invalid-key/format.json \
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
	datafile_simple.12 \
	datafile_simple.13 \
	datafile_simple.14 \
	datafile_simple.15 \
	datafile_simple.16 \
	datafile_simple.17 \
	datafile_simple.18 \
	datafile_simple.19 \
	datafile_simple.20 \
	datafile_xml.0 \
	expected_help.txt \
	listview_output.0 \
	listview_output.1 \
	listview_output.2 \
	listview_output.3 \
	listview_output.4 \
	listview_output.5 \
	listview_output.6 \
	log.clog \
	logfile_access_log.0 \
	logfile_access_log.1 \
	logfile_bad_access_log.0 \
	logfile_bad_syslog.0 \
	logfile_bro_conn.log.0 \
	logfile_bro_http.log.0 \
	logfile_blued.0 \
	logfile_crlf.0 \
	logfile_cxx.0 \
	logfile_empty.0 \
	logfile_epoch.0 \
	logfile_epoch.1 \
	logfile_filter.0 \
	logfile_for_join.0 \
	logfile_generic.0 \
	logfile_generic.1 \
	logfile_generic.2 \
	logfile_glog.0 \
	logfile_journald.json \
	logfile_json.json \
	logfile_json2.json \
	logfile_json3.json \
	logfile_leveltest.0 \
	logfile_multiline.0 \
	logfile_nested_json.json \
	logfile_openam.0 \
	logfile_plain.0 \
	logfile_pretty.0 \
	logfile_rollover.0 \
	logfile_rollover.1 \
	logfile_strace_log.0 \
	logfile_syslog.0 \
	logfile_syslog.1 \
	logfile_syslog.2 \
	logfile_syslog_fr.0 \
	logfile_syslog_with_access_log.0 \
	logfile_syslog_with_mixed_times.0 \
	logfile_tai64n.0 \
	logfile_tcf.0 \
	logfile_tcf.1 \
	logfile_tcsh_history.0 \
	logfile_vami.0 \
	logfile_vdsm.0 \
	logfile_with_a_really_long_name_to_test_a_bug_with_long_names.0 \
	multiline.lnav \
	nested.lnav \
	mvwattrline_output.0 \
	textfile_json_indented.0 \
	textfile_json_one_line.0 \
	textfile_quoted_json.0 \
	toplevel.lnav \
	UTF-8-test.txt \
	view_colors_output.0 \
	vt52_curses_input.0 \
	vt52_curses_input.1 \
	vt52_curses_output.0 \
	vt52_curses_output.1 \
	formats/collision/format.json \
	formats/customlevel/format.json \
	formats/jsontest/format.json \
	formats/jsontest/lnav-logstash.json \
	formats/jsontest/rewrite-user.lnav \
	formats/jsontest2/format.json \
	formats/jsontest3/format.json \
	formats/nestedjson/format.json \
	formats/scripts/multiline-echo.lnav \
	formats/scripts/redirecting.lnav \
	formats/scripts/nested-redirecting.lnav \
	formats/sqldir/init.sql \
	formats/timestamp/format.json \
	log-samples/sample-27353a72ba4025448f261dcfa6ea16e474187795.txt \
	log-samples/sample-70c906b3c1a1cf03f15bde92ee78edfa6f9b7960.txt \
	log-samples/sample-ad31f12d2adabd07e3ddda3ad5b0dbf6b49c4c99.txt

TESTS = \
    lnav_doctests \
    test_abbrev \
	test_ansi_scrubber \
	test_auto_fd \
	test_auto_mem \
	test_bookmarks \
	test_date_time_scanner \
	test_format_installer.sh \
	test_format_loader.sh \
	test_cli.sh \
	test_cmds.sh \
	test_line_buffer2 \
	test_line_buffer.sh \
	test_listview.sh \
	test_meta.sh \
	test_grep_proc.sh \
	test_grep_proc2 \
	test_json_format.sh \
	test_json_op.sh \
	test_json_ptr_walk.sh \
	test_log_accel \
	test_logfile.sh \
	test_pcrepp \
	test_reltime \
	test_scripts.sh \
	test_sessions.sh \
	test_shlexer.sh \
	test_sql.sh \
	test_sql_coll_func.sh \
	test_sql_json_func.sh \
	test_sql_fs_func.sh \
	test_sql_str_func.sh \
	test_sql_time_func.sh \
	test_data_parser.sh \
	test_yajlpp \
	test_pretty_print.sh

DISABLED_TESTS = \
	test_top_status \
    test_view_colors.sh \
    test_vt52_curses.sh

if HAVE_LIBCURL
TESTS += \
    test_curl.sh
endif

DISTCLEANFILES = \
	*.dat \
	*.err \
	*.db \
	*.dpt \
	*.diff \
	*.index \
	*.tmp \
	*.gz \
	*.bz2 \
	hw.txt \
	hw2.txt \
	truncfile.0 \
	logfile_append.0 \
	logfile_changed.0 \
	logfile_rollover.1.live \
	test.log \
	logfile_stdin.log \
	logfile_syslog.0 \
	logfile_syslog_fr.0 \
	logfile_syslog_with_mixed_times.0 \
	unreadable.log \
	empty \
	scripts-empty

distclean-local:
	$(RM_V)rm -rf sessions
	$(RM_V)rm -rf meta-sessions
	$(RM_V)rm -rf .lnav
	$(RM_V)rm -rf ../installer-test-home
