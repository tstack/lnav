
include $(top_srcdir)/aminclude_static.am

SUBDIRS = expected

TESTS_ENVIRONMENT = $(BASH) $(top_builddir)/TESTS_ENVIRONMENT
LOG_COMPILER = $(BASH) $(top_builddir)/TESTS_ENVIRONMENT

RM_V = $(RM_V_@AM_V@)
RM_V_ = $(RM_V_@AM_DEFAULT_V@)
RM_V_0 = @echo "  RM      " $@;

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

if HAVE_CARGO
RUST_DEPS_CPPFLAGS = -I$(top_srcdir)/src/third-party/prqlc-c -DHAVE_RUST_DEPS=1
PRQLC_DIR = ../src/third-party/prqlc-c/target
RUST_DEPS_LIBS = $(PRQLC_DIR)/release/libprqlc_c.a
else
RUST_DEPS =
RUST_DEPS_CPPFLAGS =
RUST_DEPS_LIBS =
endif

AM_CPPFLAGS = \
	-Wall \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/fmtlib \
	-I$(top_srcdir)/src/third-party \
	-I$(top_srcdir)/src/third-party/date/include \
	-I$(top_srcdir)/src/third-party/notcurses/include \
	-I$(top_srcdir)/src/third-party/scnlib/include \
	$(CODE_COVERAGE_CPPFLAGS) \
	$(LIBARCHIVE_CFLAGS) \
	$(PCRE_CFLAGS) \
	$(SQLITE3_CFLAGS) \
    $(RUST_DEPS_CPPFLAGS)

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
    document.sections.tests \
	drive_data_scanner \
	drive_doc_discovery \
	drive_line_buffer \
	drive_grep_proc \
	drive_listview \
	drive_logfile \
	drive_mvwattrline \
	drive_shlexer \
	drive_sql \
	drive_sql_anno \
	drive_textinput \
	drive_view_colors \
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
	test_reltime \
	test_text_anonymizer \
	test_top_status

AM_LDFLAGS = \
    $(LIBARCHIVE_LDFLAGS) \
	$(STATIC_LDFLAGS) \
	$(SQLITE3_LDFLAGS) \
	$(CURSES_LIB)

CONFIG_OBJS = \
	../src/ansi-palette-json.$(OBJEXT) \
	../src/default-config.$(OBJEXT) \
	../src/xterm-palette-json.$(OBJEXT)

TEXT2C_OBJS = \
	../src/builtin-scripts.$(OBJEXT) \
	../src/builtin-sh-scripts.$(OBJEXT) \
	../src/default-formats.$(OBJEXT) \
	../src/time_fmts.$(OBJEXT)

LDADD = \
    -lz \
    $(CONFIG_OBJS) \
    $(TEXT2C_OBJS) \
    $(DUMMY_OBJS) \
    ../src/lnav.events.$(OBJEXT) \
    $(top_builddir)/src/libdiag.a \
    $(top_builddir)/src/libdatascanner.a \
    $(top_builddir)/src/formats/logfmt/liblogfmt.a \
    $(top_builddir)/src/fmtlib/libcppfmt.a \
    $(top_builddir)/src/pcrepp/libpcrepp.a \
    $(top_builddir)/src/yajl/libyajl.a \
    $(top_builddir)/src/yajlpp/libyajlpp.a \
    $(top_builddir)/src/base/libbase.a \
    $(top_builddir)/src/pugixml/libpugixml.a \
	$(top_builddir)/src/tailer/libtailerservice.a \
	$(top_builddir)/src/tailer/libtailercommon.a \
	$(top_builddir)/src/tailer/libtailerpp.a \
    $(top_builddir)/src/third-party/base64/lib/libbase64.a \
    $(top_builddir)/src/third-party/date/src/libdatepp.a \
	$(top_builddir)/src/third-party/notcurses/src/lib/liblnavnotcurses.a \
    $(top_builddir)/src/third-party/scnlib/src/libscnlib.a \
    $(CURSES_LIB) \
    $(LIBARCHIVE_LIBS) \
    $(SQLITE3_LIBS) \
    $(PCRE_LIBS) \
    $(LIBCURL) \
    $(RUST_DEPS_LIBS) \
    -lunistring

test_ansi_scrubber_SOURCES = test_ansi_scrubber.cc

test_auto_fd_SOURCES = test_auto_fd.cc

test_auto_mem_SOURCES = test_auto_mem.cc

test_bookmarks_SOURCES = test_bookmarks.cc

test_date_time_scanner_SOURCES = test_date_time_scanner.cc

test_grep_proc2_SOURCES = test_grep_proc2.cc

test_line_buffer2_SOURCES = test_line_buffer2.cc

test_log_accel_SOURCES = test_log_accel.cc

test_text_anonymizer_SOURCES = test_text_anonymizer.cc

test_top_status_SOURCES = test_top_status.cc

test_abbrev_SOURCES = test_abbrev.cc

test_reltime_SOURCES = test_reltime.cc

lnav_doctests_SOURCES = lnav_doctests.cc

document_sections_tests_SOURCES = document.sections.tests.cc

drive_line_buffer_SOURCES = drive_line_buffer.cc

drive_grep_proc_SOURCES = drive_grep_proc.cc

drive_listview_SOURCES = drive_listview.cc

drive_logfile_SOURCES = drive_logfile.cc

drive_shlexer_SOURCES = drive_shlexer.cc

drive_data_scanner_SOURCES = \
	drive_data_scanner.cc

drive_doc_discovery_SOURCES = \
	drive_doc_discovery.cc

drive_mvwattrline_SOURCES = drive_mvwattrline.cc

drive_view_colors_SOURCES = drive_view_colors.cc

drive_sql_SOURCES = drive_sql.cc

drive_sql_anno_SOURCES = drive_sql_anno.cc

drive_textinput_SOURCES = drive_textinput.cc

slicer_SOURCES = slicer.cc

scripty_SOURCES = scripty.cc

dist_noinst_SCRIPTS = \
	parser_debugger.py \
	test_cli.sh \
	test_cmds.sh \
	test_config.sh \
	test_curl.sh \
	test_data_parser.sh \
	test_demux.sh \
	test_events.sh \
	test_format_installer.sh \
	test_format_loader.sh \
	test_timeline.sh \
	test_grep_proc.sh \
	test_json_format.sh \
	test_line_buffer.sh \
	test_listview.sh \
	test_logfile.sh \
	test_meta.sh \
	test_mvwattrline.sh \
	test_prql.sh \
	test_regex101.sh \
	test_remote.sh \
	test_scripts.sh \
	test_sessions.sh \
	test_shlexer.sh \
	test_sql.sh \
	test_sql_anno.sh \
	test_sql_coll_func.sh \
	test_sql_fs_func.sh \
	test_sql_indexes.sh \
	test_sql_json_func.sh \
	test_sql_regexp.sh \
	test_sql_search_table.sh \
	test_sql_str_func.sh \
	test_sql_time_func.sh \
	test_sql_views_vtab.sh \
	test_sql_xml_func.sh \
	test_sql_yaml_func.sh \
	test_text_file.sh \
	test_tui.sh \
	test_view_colors.sh \
	test_pretty_print.sh

dist_noinst_DATA = \
    expected/test_tailer.sh_12f539e535df04364316699f9edeac461aa9f9de.err \
    expected/test_tailer.sh_12f539e535df04364316699f9edeac461aa9f9de.out \
	ansi-colors.0.in \
	bad-config/formats/invalid-file-format/format.json \
	bad-config/formats/invalid-json-format/format.json \
	bad-config/formats/invalid-name/format.json \
	bad-config/formats/invalid-no-tscap/format.json \
	bad-config/formats/invalid-properties/format.json \
	bad-config/formats/invalid-regex/format.json \
	bad-config/formats/invalid-sample/format.json \
	bad-config/formats/invalid-schema/format.json \
	bad-config/formats/invalid-sql/init.sql \
	bad-config/formats/invalid-sql/init2.sql \
	bad-config/formats/no-regexes/format.json \
	bad-config/formats/no-samples/format.json \
	bad-config2/configs/invalid-annotation/config.json \
	bad-config2/configs/invalid-theme/config.json \
	bad-config2/formats/invalid-config/config.json \
	bad-config2/formats/invalid-config/config.bad-schema.json \
	bad-config2/formats/invalid-config/config.malformed.json \
	bad-config2/formats/invalid-config/config.truncated.json \
	bad-config-json/formats/invalid-json/format.json \
	bad-config-json/formats/invalid-key/format.json \
	books.json \
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
	datafile_simple.21 \
	datafile_simple.22 \
	datafile_simple.26 \
	datafile_xml.0 \
	dhcp.pcapng \
	dhcp-trunc.pcapng \
	empty-result.lnav \
	example.patch \
	example.toml \
	expected_help.txt \
	gharchive_log.jsonl \
	invalid-books.xml \
	listview_output.0 \
	listview_output.1 \
	listview_output.2 \
	listview_output.3 \
	listview_output.4 \
	listview_output.5 \
	listview_output.6 \
	listview_output_cursor.0 \
	listview_output_cursor.1 \
	listview_output_cursor.2 \
	listview_output_cursor.3 \
	listview_output_cursor.4 \
	listview_output_cursor.5 \
	listview_output_cursor.6 \
	log.clog \
	logfile_access_log.0 \
	logfile_access_log.1 \
	logfile_ansi.0 \
	logfile_ansi.1 \
	logfile_bad_access_log.0 \
	logfile_bad_syslog.0 \
	logfile_block.1 \
	logfile_block.2 \
	logfile_blued.0 \
	logfile_bro_conn.log.0 \
	logfile_bro_http.log.0 \
	logfile_bunyan.0 \
	logfile_crlf.0 \
	logfile_cloudflare.json \
	logfile_cxx.0 \
	logfile_docker_compose.0 \
	logfile_docker_compose_with_noise.0 \
	logfile_docker_compose_with_ts.0 \
	logfile_dst.0 \
	logfile_ecs_log.json \
	logfile_empty.0 \
	logfile_epoch.0 \
	logfile_epoch.1 \
	logfile_filter.0 \
	logfile_for_join.0 \
	logfile_generic.0 \
	logfile_generic.1 \
	logfile_generic.2 \
	logfile_generic.3 \
	logfile_generic_with_header.0 \
	logfile_glog.0 \
	logfile_haproxy.0 \
	logfile_invalid_json.json \
	logfile_invalid_json2.json \
	logfile_mixed_json2.json \
	logfile_mux_zookeeper.0 \
	logfile_java.0 \
	logfile_java.1 \
	logfile_journald.json \
	logfile_json.json \
	logfile_json2.json \
	logfile_json3.json \
	logfile_json_invalid.json \
	logfile_json_subsec.json \
	logfile_leveltest.0 \
	logfile_logfmt.0 \
	logfile_multiline.0 \
	logfile_nested_json.json \
	logfile_nextcloud.0 \
	logfile_openam.0 \
	logfile_partitions.0 \
	logfile_plain.0 \
	logfile_pretty.0 \
	logfile_procstate.0 \
	logfile_regexp_access_log.0 \
	logfile_rollover.0 \
	logfile_rollover.1 \
	logfile_shop_access_log.0 \
	logfile_strace_log.0 \
	logfile_syslog.0 \
	logfile_syslog.1 \
	logfile_syslog.2 \
	logfile_syslog.3 \
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
	logfile_vmw_log.0 \
	logfile_vpxd.0 \
	logfile_w3c.0 \
	logfile_w3c.1 \
	logfile_w3c.2 \
	logfile_w3c.3 \
	logfile_w3c.4 \
	logfile_w3c.5 \
	logfile_w3c.6 \
	logfile_w3c_big.0 \
	logfile_with_a_really_long_name_to_test_a_bug_with_long_names.0 \
	logfile_with_zones.0 \
	logfile_xml_msg.0 \
	logfile_yday.0 \
	man_echo.txt \
	multiline.lnav \
	mvwattrline_output.0 \
	nested.lnav \
	pyfile_0.py \
	textfile_0.md \
	textfile_ansi.0 \
	textfile_ansi_expanding.0 \
	textfile_ctrl.0 \
	textfile_json_indented.0 \
	textfile_json_long.0 \
	textfile_json_one_line.0 \
	textfile_nonl.txt \
	textfile_plain.0 \
	textfile_quoted_json.0 \
	toplevel.lnav \
	UTF-8-test.txt \
	view_colors_output.0 \
	xpath_tui.0 \
	configs/installed/anno-test.json \
	configs/installed/anno-test.sh \
	configs/installed/hw-url-handler.json \
	configs/installed/hw-url-handler.lnav \
	formats/collision/format.json \
	formats/customlevel/format.json \
	formats/jsontest/format.json \
	formats/jsontest/lnav-logstash.json \
	formats/jsontest/rewrite-user.lnav \
	formats/jsontest2/format.json \
	formats/jsontest3/format.json \
	formats/jsontest-subsec/format.json \
	formats/nestedjson/format.json \
	formats/partitions/format.json \
	formats/scripts/multiline-echo.lnav \
	formats/scripts/redirecting.lnav \
	formats/scripts/nested-redirecting.lnav \
	formats/sqldir/init.sql \
	formats/timestamp/format.json \
	formats/xmlmsg/format.json \
    log-samples/sample-057d6c669632ef9d07b6adec605f6bdeae19af27.txt \
    log-samples/sample-06aaa6f48a801f592558575d886864d6c3ab9ed4.txt \
    log-samples/sample-1aeb47c0a97d19bb7418f0172480e05e49c6e53e.txt \
    log-samples/sample-27353a72ba4025448f261dcfa6ea16e474187795.txt \
    log-samples/sample-3856ad0f551a04fde41a020158d6b33ef97c870a.txt \
    log-samples/sample-45364b3fd51af92a4ad8a309b5f4fd88.txt \
    log-samples/sample-500c9e492e04f5f58862c8086ca301de0dd976ce.txt \
    log-samples/sample-55ac97afae4b0650ccb62e2dbc8d89bb.txt \
    log-samples/sample-6049d4309f26eefb1a3406d937a9ba8a0df592a7.txt \
    log-samples/sample-62315d884afdc4155b35f905415c74bfcfd39fc2.txt \
    log-samples/sample-70c906b3c1a1cf03f15bde92ee78edfa6f9b7960.txt \
    log-samples/sample-9cf7fbb3546c676c686fac0ed096d026f46c875f.txt \
    log-samples/sample-a74570613c082c7fe283672031e18e54e8887ffb.txt \
    log-samples/sample-aca2878a2e50779c6697c0747ab1f60e4b368dcb.txt \
    log-samples/sample-ad31f12d2adabd07e3ddda3ad5b0dbf6b49c4c99.txt \
    log-samples/sample-bc6f6cf689fa5455616b4d9fbe121a48d3c9de59.txt \
    log-samples/sample-c15acd32844669d23d0cbc88ec548129ed2c592e.txt \
    log-samples/sample-c23f22c1b932b904203e018f78dead95fb89b15d.txt \
    log-samples/sample-d0d6b3fc6766caac5ac3fac4a3754ceaab785eb8.txt \
    log-samples/sample-d4a0aedc8350f64b22403eeef4eca71fbf749d2b.txt \
    log-samples/sample-d714b5e8cd354321f376ed1c0a70ec9a2f58076d.txt \
    log-samples/sample-dd7d406352ec6a11d966b6f015a9482b060f2b29.txt \
    log-samples/sample-e779d1771e34f5203ae73e85802e78002be63db6.txt \
    log-samples/sample-eef32793daf841a576d8a5cd27239d5d.txt \
    log-samples/sample-f2fba0d0b1e57f9a707ea96a8a4efcdc.txt \
    log-samples/sample-f5afbee90a8c054061c4e9ffe673293cce7761de.txt \
    log-samples/sample-fc8923633e57bacd641d80dde3ff878212230552.txt \
    naughty_files.py \
	remote-log-dir/logfile_access_log.0 \
	remote-log-dir/logfile_access_log.1 \
    tui-captures/tui_crumbs.0 \
    tui-captures/tui_echo.0 \
    tui-captures/tui_filters.0 \
    tui-captures/tui_filters.1 \
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
	test_data_parser.sh \
	test_demux.sh \
	test_events.sh \
	test_listview.sh \
	test_meta.sh \
	test_mvwattrline.sh \
	test_timeline.sh \
	test_grep_proc2 \
	test_json_format.sh \
	test_log_accel \
	test_logfile.sh \
    test_regex101.sh \
	test_reltime \
	test_scripts.sh \
	test_sessions.sh \
	test_shlexer.sh \
	test_sql.sh \
	test_sql_anno.sh \
	test_sql_coll_func.sh \
	test_sql_fs_func.sh \
	test_sql_indexes.sh \
	test_sql_json_func.sh \
	test_sql_regexp.sh \
	test_sql_search_table.sh \
	test_sql_str_func.sh \
	test_sql_time_func.sh \
	test_sql_views_vtab.sh \
	test_sql_xml_func.sh \
	test_sql_yaml_func.sh \
	test_text_anonymizer \
	test_text_file.sh \
	test_pretty_print.sh \
    test_view_colors.sh

if HAVE_CARGO
TESTS += test_prql.sh
endif

DISABLED_TESTS = \
	test_grep_proc.sh \
	test_remote.sh \
	test_top_status \
	test_line_buffer2 \
    test_line_buffer.sh \
    test_tui.sh

if HAVE_LIBCURL
TESTS += \
    test_curl.sh
endif

DISTCLEANFILES = \
	*.cmd \
	*.dat \
	*.out \
	*.err \
	*.db \
	*.dpt \
	*.diff \
	*.index \
	*.tmp \
	*.gz \
	*.bz2 \
	*.outbak \
	*.errbak \
	*.tmpbak \
	*.xz \
	exported-session.0.lnav \
	hw.txt \
	hw2.txt \
	reload_test.0 \
	truncfile.0 \
	ln.dbg \
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
	textfile_long_lines.0 \
	not:a:remote:file \
	rollover_in.0 \
	syslog_log.sql \
	test-logs.tgz \
	test-logs-trunc.tgz \
	test_pretty_in.* \
	tmp \
	unreadable.log \
	UTF-8-test.md \
	empty \
	scripts-empty

test_remote.sh.log: remote/ssh_host_dsa_key remote/ssh_host_rsa_key remote/id_rsa

distclean-local:
	$(RM_V)rm -rf remote remote-tmp not:a:remote:dir
	$(RM_V)rm -rf sessions
	$(RM_V)rm -rf cfg
	$(RM_V)rm -rf file-tz
	$(RM_V)rm -rf tmp
	$(RM_V)rm -rf piper-tmp
	$(RM_V)rm -rf rotmp
	$(RM_V)rm -rf meta-sessions
	$(RM_V)rm -rf mgmt-config
	$(RM_V)rm -rf naughty
	$(RM_V)rm -rf piper-config
	$(RM_V)rm -rf nested
	$(RM_V)rm -rf test-config
	$(RM_V)rm -rf .lnav
	$(RM_V)rm -rf regex101-home
	$(RM_V)rm -rf events-home
	$(RM_V)rm -rf support-dump
	$(RM_V)rm -rf tui-home
	$(RM_V)rm -rf ../installer-test-home

expected:
	$(top_srcdir)/update_expected_output.sh $(srcdir) $(builddir)

.PHONY: expected
