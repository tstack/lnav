
bin_PROGRAMS = lnav

noinst_PROGRAMS = bin2c ptimec lnav-test

noinst_LIBRARIES = libdiag.a

PTIME_V = $(PTIME_V_@AM_V@)
PTIME_V_ = $(PTIME_V_@AM_DEFAULT_V@)
PTIME_V_0 = @echo "  TIMEFMT " $@;

BIN2C_V = $(BIN2C_V_@AM_V@)
BIN2C_V_ = $(BIN2C_V_@AM_DEFAULT_V@)
BIN2C_V_0 = @echo "  BIN2C   " $@;

RE2C_V = $(RE2C_V_@AM_V@)
RE2C_V_ = $(RE2C_V_@AM_DEFAULT_V@)
RE2C_V_0 = @echo "  RE2C   " $@;

help.c: $(srcdir)/help.txt bin2c
	$(BIN2C_V)./bin2c -z -c $(srcdir)/help.txt $@

dump-pid-sh.c: $(srcdir)/dump-pid.sh bin2c
	$(BIN2C_V)./bin2c -z -c $(srcdir)/dump-pid.sh $@

default-log-formats-json.c: $(srcdir)/default-log-formats.json bin2c
	$(BIN2C_V)./bin2c -z -c $(srcdir)/default-log-formats.json $@

default-config-json.c: $(srcdir)/default-config.json bin2c
	$(BIN2C_V)./bin2c -z -c $(srcdir)/default-config.json $@

init-sql.c: bin2c init.sql
	$(BIN2C_V)./bin2c -z -c $(srcdir)/init.sql $@

%.c: $(srcdir)/%.lnav bin2c
	$(BIN2C_V)./bin2c -z -c $< $@

%.c: $(srcdir)/%.json bin2c
	$(BIN2C_V)./bin2c -z -c $< $@

TIME_FORMATS = \
    "@%@" \
    "%Y-%m-%d %H:%M:%S" \
    "%Y-%m-%d %H:%M:%S%z" \
    "%Y-%m-%d %H:%M:%S %z" \
    "%Y-%m-%d %H:%M" \
    "%Y-%m-%dT%H:%M:%S.%f%z" \
    "%y-%m-%dT%H:%M:%S.%f%z" \
    "%Y-%m-%dT%H:%M:%SZ" \
    "%Y-%m-%dT%H:%M:%S%z" \
    "%Y-%m-%dT%H:%M:%S" \
    "%Y-%m-%dT%H:%M:%S%z" \
    "%Y/%m/%d %H:%M:%S" \
    "%Y/%m/%d %H:%M:%S %z" \
    "%Y/%m/%d %H:%M:%S%z" \
    "%Y/%m/%d %H:%M" \
    "%Y %b %d %a %H:%M:%S.%L" \
    "%Y %b %d %H:%M:%S.%L" \
    "%Y %b %d %H:%M:%S" \
    "%a %b %d %H:%M:%S %Y" \
    "%a %b %d %H:%M:%S.%f %Y" \
    "%a %b %d %H:%M:%S %Z %Y" \
    "%a %b %d %H:%M:%S " \
    "%a %b %d %H:%M:%S.%L " \
    "%d/%b/%Y:%H:%M:%S +0000" \
    "%d/%b/%Y:%H:%M:%S %z" \
    "%d-%b-%Y %H:%M:%S %z" \
    "%d-%b-%Y %H:%M:%S %Z" \
    "%d %b %Y %H:%M:%S" \
    "%d %b %Y %H:%M:%S.%L" \
    "%d %b %Y %H:%M:%S,%L" \
    "%b %d %H:%M:%S" \
    "%b %d %k:%M:%S" \
    "%b %d %l:%M:%S" \
    "%b %e, %Y %l:%M:%S %p" \
    "%m/%d/%y %H:%M:%S" \
    "%m/%d/%Y %I:%M:%S:%L %p %Z" \
    "%m/%d/%Y %I:%M:%S %p %Z" \
    "%m/%d/%Y %l:%M:%S %p %Z" \
    "%m/%e/%Y %I:%M:%S %p" \
    "%m/%e/%Y %l:%M:%S %p" \
    "%d/%b/%y %H:%M:%S" \
    "%m%d %H:%M:%S" \
    "%H:%M:%S" \
    "%M:%S" \
    "%m/%d %H:%M:%S" \
    "%Y-%m-%d" \
    "%Y-%m" \
    "%Y/%m/%d" \
    "%Y/%m" \
    "%s.%f" \
    $()

time_fmts.cc: ptimec
	$(PTIME_V)./ptimec $(TIME_FORMATS) > $@

if HAVE_RE2C
%.cc: %.re
	$(RE2C_V)$(RE2C_CMD) --tags -8 -o $@ $<
	$(REC2_V)test $@ -ef $(srcdir)/$*.cc || cp $@ $(srcdir)/$*.cc
endif

AM_LDFLAGS = \
	$(STATIC_LDFLAGS) \
	$(READLINE_LDFLAGS) \
	$(SQLITE3_LDFLAGS) \
	$(PCRE_LDFLAGS) \
	-pthread

AM_CPPFLAGS = \
	-DSYSCONFDIR='"$(sysconfdir)"' \
	-Wall \
	$(READLINE_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(LIBCURL_CPPFLAGS)

LDADD = \
	libdiag.a \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	$(LIBCURL) \
	-lpcrecpp

dist_noinst_DATA = \
	alpha-release.sh \
	default-log-formats.json \
	default-config.json \
	dhclient-summary.lnav \
	dump-pid.sh \
	lnav-pop-view.lnav \
	keymap-default.json \
	partition-by-boot.lnav \
	search-for.lnav

noinst_HEADERS = \
	all_logs_vtab.hh \
	ansi_scrubber.hh \
	attr_line.hh \
	auto_fd.hh \
	auto_mem.hh \
	auto_pid.hh \
	big_array.hh \
	bookmarks.hh \
	bottom_status_source.hh \
	byte_array.hh \
	column_namer.hh \
	command_executor.hh \
	curl_looper.hh \
	data_scanner.hh \
	data_scanner_re.re \
	data_parser.hh \
	default-log-formats-json.hh \
	db_sub_source.hh \
	doc_status_source.hh \
	doctest.hh \
	elem_to_json.hh \
	environ_vtab.hh \
	field_overlay_source.hh \
	file_vtab.hh \
	filter_observer.hh \
	filter_status_source.hh \
	filter_sub_source.hh \
	format-text-files.hh \
	fstat_vtab.hh \
	fts_fuzzy_match.hh \
	grep_highlighter.hh \
	grep_proc.hh \
	help.hh \
	help.txt \
	help_text_formatter.hh \
	highlighter.hh \
	hist_source.hh \
	hotkeys.hh \
	init.sql \
	init-sql.hh \
	input_dispatcher.hh \
	intern_string.hh \
	is_utf8.hh \
	json_op.hh \
	json_ptr.hh \
	k_merge_tree.h \
	line_buffer.hh \
	listview_curses.hh \
	lnav.hh \
	lnav_commands.hh \
	lnav_config.hh \
	lnav_log.hh \
	lnav_util.hh \
	log_accel.hh \
	log_actions.hh \
    log_data_helper.hh \
    log_data_table.hh \
	log_format.hh \
	log_format_loader.hh \
	log_gutter_source.hh \
	log_level.hh \
	log_level_re.re \
	log_search_table.hh \
	logfile.hh \
	logfile_sub_source.hh \
	mapbox/recursive_wrapper.hpp \
	mapbox/variant.hpp \
	mapbox/variant_io.hpp \
	mapbox/variant_visitor.hpp \
	optional.hpp \
	papertrail_proc.hh \
	pcrepp.hh \
	piper_proc.hh \
	plain_text_source.hh \
	pretty_printer.hh \
	preview_status_source.hh \
	pthreadpp.hh \
	ptimec.hh \
	readline_callbacks.hh \
	readline_curses.hh \
	readline_highlighters.hh \
	readline_possibilities.hh \
	regexp_vtab.hh \
	relative_time.hh \
	ring_span.hh \
	sequence_matcher.hh \
	sequence_sink.hh \
	session_data.hh \
	shared_buffer.hh \
	shlex.hh \
	simdutf8check.h \
	spectro_source.hh \
	sql_util.hh \
	sqlite-extension-func.hh \
	statusview_curses.hh \
	strnatcmp.h \
	strong_int.hh \
	sysclip.hh \
	termios_guard.hh \
	term_extra.hh \
	text_format.hh \
	textfile_highlighters.hh \
	textfile_sub_source.hh \
	textview_curses.hh \
	time_T.hh \
	timer.hh \
	top_status_source.hh \
	unique_path.hh \
	url_loader.hh \
	view_curses.hh \
	views_vtab.hh \
	vt52_curses.hh \
	vtab_module.hh \
	log_vtab_impl.hh \
	log_format_impls.cc \
	xterm_mouse.hh \
	xterm-palette.hh \
	yajlpp.hh \
	yajlpp_def.hh \
	spookyhash/SpookyV2.h \
	filesystem/fwd.h \
	filesystem/path.h \
	filesystem/resolver.h

if USE_INCLUDED_YAJL
noinst_HEADERS += yajl/api/yajl_common.h \
	yajl/api/yajl_gen.h \
	yajl/api/yajl_parse.h \
	yajl/api/yajl_tree.h \
	yajl/yajl_alloc.h \
	yajl/yajl_buf.h \
	yajl/yajl_bytestack.h \
	yajl/yajl_common.h \
	yajl/yajl_encode.h \
	yajl/yajl_lex.h \
	yajl/yajl_parser.h \
	yajl/yajl_version.h
endif


libdiag_a_SOURCES = \
	ansi_scrubber.cc \
	bookmarks.cc \
	bottom_status_source.cc \
	collation-functions.cc \
	command_executor.cc \
	curl_looper.cc \
	db_sub_source.cc \
	elem_to_json.cc \
	environ_vtab.cc \
	extension-functions.cc \
	field_overlay_source.cc \
	file_vtab.cc \
	filter_observer.cc \
	filter_status_source.cc \
	filter_sub_source.cc \
	fstat_vtab.cc \
    fs-extension-functions.cc \
    fts_fuzzy_match.cc \
	grep_proc.cc \
	help_text_formatter.cc \
	hist_source.cc \
	hotkeys.cc \
	input_dispatcher.cc \
	intern_string.cc \
	is_utf8.cc \
	json-extension-functions.cc \
	json_op.cc \
	json_ptr.cc \
	line_buffer.cc \
	listview_curses.cc \
	lnav_commands.cc \
	lnav_config.cc \
	lnav_log.cc \
	lnav_util.cc \
	log_accel.cc \
	log_actions.cc \
	log_format.cc \
	log_format_loader.cc \
	log_level.cc \
	log_level_re.cc \
	logfile.cc \
	logfile_sub_source.cc \
	network-extension-functions.cc \
	data_scanner.cc \
	data_scanner_re.cc \
	data_parser.cc \
	papertrail_proc.cc \
	pretty_printer.cc \
	ptimec_rt.cc \
	readline_callbacks.cc \
	readline_curses.cc \
	readline_highlighters.cc \
	readline_possibilities.cc \
	regexp_vtab.cc \
	relative_time.cc \
	session_data.cc \
	sequence_matcher.cc \
	shared_buffer.cc \
	shlex.cc \
	sqlite-extension-func.cc \
	statusview_curses.cc \
	string-extension-functions.cc \
	text_format.cc \
	timer.cc \
	pcrepp.cc \
	piper_proc.cc \
	sql_util.cc \
	state-extension-functions.cc \
	strnatcmp.c \
	sysclip.cc \
	textfile_highlighters.cc \
	textview_curses.cc \
	time-extension-functions.cc \
	time_fmts.cc \
	view_curses.cc \
	view_helpers.cc \
	views_vtab.cc \
	vt52_curses.cc \
	vtab_module.cc \
	log_vtab_impl.cc \
	xterm_mouse.cc \
	xterm-palette.c \
	yajlpp.cc \
	spookyhash/SpookyV2.cpp

if USE_INCLUDED_YAJL
libdiag_a_SOURCES += yajl/yajl.c \
	yajl/yajl_alloc.c \
	yajl/yajl_alloc.h \
	yajl/yajl_buf.c \
	yajl/yajl_buf.h \
	yajl/yajl_bytestack.h \
	yajl/yajl_encode.c \
	yajl/yajl_encode.h \
	yajl/yajl_gen.c \
	yajl/yajl_lex.c \
	yajl/yajl_lex.h \
	yajl/yajl_parser.c \
	yajl/yajl_parser.h \
	yajl/yajl_tree.c \
	yajl/yajl_version.c
endif

TEXT2C_FILES = \
	dump-pid-sh.o \
	help.o \
	init-sql.o \
	default-log-formats-json.o \
	default-config-json.o \
	dhclient-summary.o \
	keymap-default.o \
	lnav-pop-view.o \
	partition-by-boot.o \
	search-for.o \
	xterm-palette.o

lnav_SOURCES = lnav.cc $(HELP_SRC)
lnav_LDADD = \
	$(TEXT2C_FILES) \
	$(LDADD)

lnav_test_SOURCES = lnav.cc $(HELP_SRC) test_override.c
lnav_test_LDADD = \
	$(TEXT2C_FILES) \
	$(LDADD)

bin2c_SOURCES = bin2c.c
bin2c_LDADD =

ptimec_SOURCES = ptimec.cc
ptimec_LDADD =

DISTCLEANFILES = \
	data_scanner_re.cc \
	default-config-json.c \
	default-log-formats-json.c \
	dump-pid-sh.c \
	help.c \
	init-sql.c \
	log_level_re.cc \
    time_fmts.cc \
    xterm-palette.c

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))

install-exec-hook:
	bash $(srcdir)/alpha-release.sh
