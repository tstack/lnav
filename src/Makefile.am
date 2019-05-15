
SUBDIRS = base fmtlib pcrepp yajl yajlpp .

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

CONFIG_FILES = \
    $(srcdir)/root-config.json \
    $(srcdir)/keymaps/default-keymap.json \
    $(srcdir)/themes/default-theme.json \
    $(srcdir)/themes/eldar.json \
    $(srcdir)/themes/monocai.json \
    $(srcdir)/themes/night-owl.json \
    $(srcdir)/themes/solarized-dark.json \
    $(srcdir)/themes/solarized-light.json \
    $()

default-config.h default-config.c: bin2c $(CONFIG_FILES)
	$(BIN2C_V)./bin2c -n lnav_config_json default-config $(CONFIG_FILES)

BUILTIN_LNAVSCRIPTS = \
    $(srcdir)/scripts/dhclient-summary.lnav \
    $(srcdir)/scripts/lnav-pop-view.lnav \
    $(srcdir)/scripts/partition-by-boot.lnav \
    $(srcdir)/scripts/search-for.lnav \
    $()

builtin-scripts.h builtin-scripts.c: bin2c $(BUILTIN_LNAVSCRIPTS)
	$(BIN2C_V)./bin2c -n lnav_scripts builtin-scripts $(BUILTIN_LNAVSCRIPTS)

BUILTIN_SHSCRIPTS = \
    $(srcdir)/scripts/dump-pid.sh \
    $()

builtin-sh-scripts.h builtin-sh-scripts.c: bin2c $(BUILTIN_SHSCRIPTS)
	$(BIN2C_V)./bin2c -n lnav_sh_scripts builtin-sh-scripts $(BUILTIN_SHSCRIPTS)

%-sh.c: $(srcdir)/%.sh bin2c
	$(BIN2C_V)./bin2c $(*)-sh $<

%-txt.c %-txt.h: $(srcdir)/%.txt bin2c
	$(BIN2C_V)./bin2c $(*)-txt $<

%-sql.c %-sql.h: $(srcdir)/%.sql bin2c
	$(BIN2C_V)./bin2c $(*)-sql $<

%-lnav.c %-lnav.h: $(srcdir)/%.lnav bin2c
	$(BIN2C_V)./bin2c $(*)-lnav $<

%-json.c %-json.h: $(srcdir)/%.json bin2c
	$(BIN2C_V)./bin2c $(*)-json $<

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
	-I$(srcdir)/fmtlib \
	-Wall \
	$(READLINE_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(LIBCURL_CPPFLAGS)

LDADD = \
	libdiag.a \
	base/libbase.a \
	fmtlib/libcppfmt.a \
	pcrepp/libpcrepp.a \
	yajl/libyajl.a \
	yajlpp/libyajlpp.a \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	$(LIBCURL) \
	-lpcrecpp

dist_noinst_DATA = \
	alpha-release.sh \
	ansi-palette.json \
	default-log-formats.json \
	keymaps/default-keymap.json \
	root-config.json \
	scripts/dhclient-summary.lnav \
	scripts/dump-pid.sh \
	scripts/lnav-pop-view.lnav \
	scripts/partition-by-boot.lnav \
	scripts/search-for.lnav \
	themes/default-theme.json \
	themes/eldar.json \
    themes/monocai.json \
    themes/night-owl.json \
	themes/solarized-dark.json \
	themes/solarized-light.json \
	xterm-palette.json

BUILT_SOURCES = \
    ansi-palette-json.h \
    ansi-palette-json.c \
    builtin-scripts.h \
    builtin-scripts.c \
    builtin-sh-scripts.h \
    builtin-sh-scripts.c \
    default-config.h \
    default-config.c \
    default-log-formats-json.h \
    default-log-formats-json.c \
    help-txt.h \
    help-txt.c \
    init-sql.h \
    init-sql.c \
    xterm-palette-json.h \
    xterm-palette-json.c

noinst_HEADERS = \
	all_logs_vtab.hh \
	ansi_scrubber.hh \
	attr_line.hh \
	auto_fd.hh \
	auto_mem.hh \
	auto_pid.hh \
	big_array.hh \
	bin2c.h \
	bookmarks.hh \
	bottom_status_source.hh \
	builtin-scripts.h \
	byte_array.hh \
	column_namer.hh \
	command_executor.hh \
	curl_looper.hh \
	data_scanner.hh \
	data_scanner_re.re \
	data_parser.hh \
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
	fstat_vtab.hh \
	fts_fuzzy_match.hh \
	grep_highlighter.hh \
	grep_proc.hh \
	help.txt \
	help_text.hh \
	help_text_formatter.hh \
	highlighter.hh \
	hist_source.hh \
	hotkeys.hh \
	init.sql \
	init-sql.h \
	input_dispatcher.hh \
	intern_string.hh \
	is_utf8.hh \
	k_merge_tree.h \
	line_buffer.hh \
	listview_curses.hh \
	lnav.hh \
	lnav_commands.hh \
	lnav_config.hh \
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
	piper_proc.hh \
	plain_text_source.hh \
	pretty_printer.hh \
	preview_status_source.hh \
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
	styling.hh \
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
	spookyhash/SpookyV2.h \
	filesystem/fwd.h \
	filesystem/path.h \
	filesystem/resolver.h \
    fmtlib/fmt/time.h \
    fmtlib/fmt/ostream.h \
    fmtlib/fmt/format-inl.h \
    fmtlib/fmt/ranges.h \
    fmtlib/fmt/core.h \
    fmtlib/fmt/locale.h \
    fmtlib/fmt/chrono.h \
    fmtlib/fmt/color.h \
    fmtlib/fmt/printf.h \
    fmtlib/fmt/posix.h \
    fmtlib/fmt/format.h

libdiag_a_SOURCES = \
    $(BUILT_SOURCES) \
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
	line_buffer.cc \
	listview_curses.cc \
	lnav_commands.cc \
	lnav_config.cc \
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
	spookyhash/SpookyV2.cpp

TEXT2C_FILES = \
	ansi-palette-json.o \
	builtin-scripts.o \
	builtin-sh-scripts.o \
	help-txt.o \
	init-sql.o \
	default-log-formats-json.o \
	default-config.o \
	xterm-palette-json.o

lnav_SOURCES = lnav.cc
lnav_LDADD = \
	$(TEXT2C_FILES) \
	$(LDADD)

lnav_test_SOURCES = lnav.cc test_override.c
lnav_test_LDADD = \
	$(TEXT2C_FILES) \
	$(LDADD)

bin2c_SOURCES = bin2c.c
bin2c_LDADD =

ptimec_SOURCES = ptimec.cc
ptimec_LDADD =

DISTCLEANFILES = \
    $(BUILT_SOURCES) \
	data_scanner_re.cc \
	default-config.json \
	default-config.c \
	default-log-formats-json.c \
	help-txt.c \
	init-sql.c \
	log_level_re.cc \
    time_fmts.cc

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))

install-exec-hook:
	bash $(srcdir)/alpha-release.sh
