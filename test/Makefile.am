
include $(top_srcdir)/aminclude_static.am

TESTS_ENVIRONMENT = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT
LOG_COMPILER = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT

RM_V = $(RM_V_@AM_V@)
RM_V_ = $(RM_V_@AM_DEFAULT_V@)
RM_V_0 = @echo "  RM      " $@;

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

AM_CPPFLAGS = \
	-Wall \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/fmtlib \
	$(CODE_COVERAGE_CPPFLAGS) \
	$(LIBARCHIVE_CFLAGS) \
	$(READLINE_CFLAGS) \
	$(SQLITE3_CFLAGS)

# AM_CFLAGS = -fprofile-arcs -ftest-coverage
# AM_CXXFLAGS = -fprofile-arcs -ftest-coverage

remote/ssh_host_rsa_key:
	mkdir -p remote
	ssh-keygen -f remote/ssh_host_rsa_key -N '' -t rsa

remote/ssh_host_dsa_key:
	mkdir -p remote
	ssh-keygen -f remote/ssh_host_dsa_key -N '' -t dsa

remote/id_rsa:
	mkdir -p remote
	ssh-keygen -f remote/id_rsa -N '' -t rsa
	cp -f remote/id_rsa.pub remote/authorized_keys

noinst_LIBRARIES = \
	libtestdummy.a

libtestdummy_a_SOURCES = \
	test_stubs.cc

DUMMY_OBJS = \
	test_stubs.$(OBJEXT)

check_PROGRAMS = \
	drive_data_scanner \
	drive_line_buffer \
	drive_grep_proc \
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
	test_line_buffer2 \
	test_log_accel \
	test_ncurses_unicode \
	test_reltime \
	test_top_status

AM_LDFLAGS = \
    $(LIBARCHIVE_LDFLAGS) \
	$(STATIC_LDFLAGS) \
	$(SQLITE3_LDFLAGS) \
	$(READLINE_LDFLAGS) \
	$(CURSES_LIB)

CONFIG_OBJS = \
	../src/ansi-palette-json.$(OBJEXT) \
	../src/default-config.$(OBJEXT) \
	../src/xterm-palette-json.$(OBJEXT)

TEXT2C_OBJS = \
	../src/builtin-scripts.$(OBJEXT) \
	../src/builtin-sh-scripts.$(OBJEXT) \
	../src/default-formats.$(OBJEXT) \
	../src/help-txt.$(OBJEXT) \
	../src/time_fmts.$(OBJEXT)

LDADD = \
    -lz \
    $(CONFIG_OBJS) \
    $(TEXT2C_OBJS) \
    $(DUMMY_OBJS) \
    $(top_builddir)/src/libdiag.a \
    $(top_builddir)/src/fmtlib/libcppfmt.a \
    $(top_builddir)/src/pcrepp/libpcrepp.a \
    $(top_builddir)/src/yajl/libyajl.a \
    $(top_builddir)/src/yajlpp/libyajlpp.a \
    $(top_builddir)/src/base/libbase.a \
    $(top_builddir)/src/pugixml/libpugixml.a \
    $(READLINE_LIBS) \
    $(CURSES_LIB) \
    $(LIBARCHIVE_LIBS) \
    $(SQLITE3_LIBS) \
    $(PCRE_LIBS) \
    $(LIBCURL)

test_ansi_scrubber_SOURCES = test_ansi_scrubber.cc

test_auto_fd_SOURCES = test_auto_fd.cc

test_auto_mem_SOURCES = test_auto_mem.cc

test_bookmarks_SOURCES = test_bookmarks.cc

test_date_time_scanner_SOURCES = test_date_time_scanner.cc

test_grep_proc2_SOURCES = test_grep_proc2.cc

test_line_buffer2_SOURCES = test_line_buffer2.cc

test_log_accel_SOURCES = test_log_accel.cc

test_top_status_SOURCES = test_top_status.cc

test_abbrev_SOURCES = test_abbrev.cc

test_reltime_SOURCES = test_reltime.cc

test_ncurses_unicode_SOURCES = test_ncurses_unicode.cc

lnav_doctests_SOURCES = lnav_doctests.cc

drive_line_buffer_SOURCES = drive_line_buffer.cc

drive_grep_proc_SOURCES = drive_grep_proc.cc

drive_listview_SOURCES = drive_listview.cc

drive_logfile_SOURCES = drive_logfile.cc

drive_sequencer_SOURCES = drive_sequencer.cc

drive_shlexer_SOURCES = drive_shlexer.cc

drive_data_scanner_SOURCES = \
	drive_data_scanner.cc

drive_mvwattrline_SOURCES = drive_mvwattrline.cc

drive_view_colors_SOURCES = drive_view_colors.cc

drive_vt52_curses_SOURCES = drive_vt52_curses.cc

drive_readline_curses_SOURCES = drive_readline_curses.cc

drive_sql_SOURCES = drive_sql.cc

drive_sql_anno_SOURCES = drive_sql_anno.cc

slicer_SOURCES = slicer.cc

scripty_SOURCES = scripty.cc

dist_noinst_SCRIPTS = \
	parser_debugger.py \
	test_cli.sh \
	test_cmds.sh \
	test_config.sh \
	test_curl.sh \
	test_data_parser.sh \
	test_format_installer.sh \
	test_format_loader.sh \
	test_grep_proc.sh \
	test_json_format.sh \
	test_line_buffer.sh \
	test_listview.sh \
	test_logfile.sh \
	test_meta.sh \
	test_mvwattrline.sh \
	test_remote.sh \
	test_scripts.sh \
	test_sessions.sh \
	test_shlexer.sh \
	test_sql.sh \
	test_sql_anno.sh \
	test_sql_coll_func.sh \
	test_sql_json_func.sh \
	test_sql_str_func.sh \
	test_sql_time_func.sh \
	test_sql_xml_func.sh \
	test_sql_fs_func.sh \
	test_tui.sh \
	test_view_colors.sh \
	test_vt52_curses.sh \
	test_pretty_print.sh

dist_noinst_DATA = \
	ansi-colors.0.in \
	bad-config/formats/invalid-regex/format.json \
	bad-config/formats/invalid-sample/format.json \
	bad-config/formats/invalid-sql/init.sql \
	bad-config/formats/no-samples/format.json \
	bad-config2/formats/invalid-config/config.json \
	bad-config2/formats/invalid-config/config.malformed.json \
	bad-config2/formats/invalid-config/config.truncated.json \
	bad-config-json/formats/invalid-json/format.json \
	bad-config-json/formats/invalid-key/format.json \
	books.xml \
	file_for_dot_read.sql \
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
	logfile_generic_with_header.0 \
	logfile_glog.0 \
	logfile_haproxy.0 \
	logfile_invalid_json.json \
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
	logfile_syslog_with_header.0 \
	logfile_syslog_with_mixed_times.0 \
	logfile_tai64n.0 \
	logfile_tcf.0 \
	logfile_tcf.1 \
	logfile_tcsh_history.0 \
	logfile_uwsgi.0 \
	logfile_vami.0 \
	logfile_vdsm.0 \
	logfile_w3c.0 \
	logfile_w3c.1 \
	logfile_w3c.2 \
	logfile_w3c.3 \
	logfile_w3c.4 \
	logfile_w3c.5 \
	logfile_w3c.6 \
	logfile_w3c_big.0 \
	logfile_with_a_really_long_name_to_test_a_bug_with_long_names.0 \
	logfile_xml_msg.0 \
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
	xpath_tui.0 \
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
	formats/xmlmsg/format.json \
	log-samples/sample-27353a72ba4025448f261dcfa6ea16e474187795.txt \
	log-samples/sample-70c906b3c1a1cf03f15bde92ee78edfa6f9b7960.txt \
	log-samples/sample-ad31f12d2adabd07e3ddda3ad5b0dbf6b49c4c99.txt \
	remote-log-dir/logfile_access_log.0 \
	remote-log-dir/logfile_access_log.1 \
    tui-captures/tui_echo.0 \
	tui-captures/tui_help.0

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
	test_config.sh \
	test_listview.sh \
	test_meta.sh \
	test_mvwattrline.sh \
	test_grep_proc.sh \
	test_grep_proc2 \
	test_json_format.sh \
	test_log_accel \
	test_logfile.sh \
	test_reltime \
	test_remote.sh \
	test_scripts.sh \
	test_sessions.sh \
	test_shlexer.sh \
	test_sql.sh \
	test_sql_anno.sh \
	test_sql_coll_func.sh \
	test_sql_json_func.sh \
	test_sql_fs_func.sh \
	test_sql_str_func.sh \
	test_sql_time_func.sh \
	test_sql_xml_func.sh \
	test_tui.sh \
	test_data_parser.sh \
	test_pretty_print.sh \
    test_view_colors.sh \
	test_vt52_curses.sh

DISABLED_TESTS = \
	test_top_status \
	test_line_buffer2 \
    test_line_buffer.sh

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
	*.xz \
	hw.txt \
	hw2.txt \
	reload_test.0 \
	truncfile.0 \
	logfile_append.0 \
	logfile_changed.0 \
	logfile_rollover.1.live \
	test.log \
	logfile_stdin.log \
	logfile_stdin.0.log \
	logfile_syslog_test.0 \
	logfile_syslog_test.2 \
	logfile_syslog_fr_test.0 \
	logfile_syslog_with_mixed_times_test.0 \
	not:a:remote:file \
	test-logs.tgz \
	test-logs-trunc.tgz \
	test_pretty_in.* \
	tmp \
	unreadable.log \
	empty \
	scripts-empty

all-local: remote/ssh_host_dsa_key remote/ssh_host_rsa_key remote/id_rsa

distclean-local:
	$(RM_V)rm -rf remote remote-tmp not:a:remote:dir
	$(RM_V)rm -rf sessions
	$(RM_V)rm -rf tmp
	$(RM_V)rm -rf rotmp
	$(RM_V)rm -rf meta-sessions
	$(RM_V)rm -rf nested
	$(RM_V)rm -rf test-config
	$(RM_V)rm -rf .lnav
	$(RM_V)rm -rf ../installer-test-home
