
include $(top_srcdir)/aminclude_static.am

SUBDIRS = tools fmtlib pcrepp base tailer pugixml yajl yajlpp .

bin_PROGRAMS = lnav

noinst_PROGRAMS = lnav-test

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

include formats/formats.am

default-formats.h default-formats.cc: tools/bin2c$(BUILD_EXEEXT) $(FORMAT_FILES)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) -n lnav_format_json default-formats $(FORMAT_FILES)

include keymaps/keymaps.am
include themes/themes.am

CONFIG_FILES = \
    $(srcdir)/root-config.json \
    $(KEYMAP_FILES) \
    $(THEME_FILES) \
    $()

default-config.h default-config.cc: tools/bin2c$(BUILD_EXEEXT) $(CONFIG_FILES)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) -n lnav_config_json default-config $(CONFIG_FILES)

include scripts/scripts.am

builtin-scripts.h builtin-scripts.cc: tools/bin2c$(BUILD_EXEEXT) $(BUILTIN_LNAVSCRIPTS)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) -n lnav_scripts builtin-scripts $(BUILTIN_LNAVSCRIPTS)

builtin-sh-scripts.h builtin-sh-scripts.cc: tools/bin2c$(BUILD_EXEEXT) $(BUILTIN_SHSCRIPTS)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) -n lnav_sh_scripts builtin-sh-scripts $(BUILTIN_SHSCRIPTS)

%-sh.cc: $(srcdir)/%.sh tools/bin2c$(BUILD_EXEEXT)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) $(*)-sh $<

%-txt.cc %-txt.h: $(srcdir)/%.txt tools/bin2c$(BUILD_EXEEXT)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) $(*)-txt $<

%-sql.cc %-sql.h: $(srcdir)/%.sql tools/bin2c$(BUILD_EXEEXT)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) $(*)-sql $<

%-lnav.cc %-lnav.h: $(srcdir)/%.lnav tools/bin2c$(BUILD_EXEEXT)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) $(*)-lnav $<

%-json.cc %-json.h: $(srcdir)/%.json tools/bin2c$(BUILD_EXEEXT)
	$(BIN2C_V)tools/bin2c$(BUILD_EXEEXT) $(*)-json $<

include time_formats.am

time_fmts.cc: ptimec$(BUILD_EXEEXT)
	$(PTIME_V)./ptimec$(BUILD_EXEEXT) $(TIME_FORMATS) > $@

if HAVE_RE2C
%.cc: %.re
	$(RE2C_V)$(RE2C_CMD) --tags -8 -o $@ $<
	$(REC2_V)test $@ -ef $(srcdir)/$*.cc || cp $@ $(srcdir)/$*.cc
endif

lnav_config.$(OBJEXT): default-config.h

log_format_loader.$(OBJEXT): \
	builtin-scripts.h \
	builtin-sh-scripts.h \
	default-formats.h

styling.$(OBJEXT): ansi-palette-json.h xterm-palette-json.h

view_helpers.$(OBJEXT): help-txt.h

LNAV_BUILT_FILES = \
    ansi-palette-json.h \
    ansi-palette-json.cc \
    builtin-scripts.h \
    builtin-scripts.cc \
    builtin-sh-scripts.h \
    builtin-sh-scripts.cc \
    default-config.h \
    default-config.cc \
    default-formats.h \
    default-formats.cc \
    help-txt.h \
    help-txt.cc \
    init-sql.h \
    init-sql.cc \
    time_fmts.cc \
    xterm-palette-json.h \
    xterm-palette-json.cc

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

AM_LDFLAGS = \
	$(STATIC_LDFLAGS) \
	$(LIBARCHIVE_LDFLAGS) \
	$(READLINE_LDFLAGS) \
	$(SQLITE3_LDFLAGS) \
	$(PCRE_LDFLAGS)

AM_CPPFLAGS = \
	-DSYSCONFDIR='"$(sysconfdir)"' \
	-DSQLITE_OMIT_LOAD_EXTENSION \
	-I$(srcdir)/fmtlib \
	-I$(srcdir)/third-party \
	-Wall \
	$(CODE_COVERAGE_CPPFLAGS) \
	$(LIBARCHIVE_CFLAGS) \
	$(READLINE_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(LIBCURL_CPPFLAGS)

LDADD = \
	libdiag.a \
	base/libbase.a \
	fmtlib/libcppfmt.a \
	pcrepp/libpcrepp.a \
	pugixml/libpugixml.a \
	tailer/libtailerservice.a \
	tailer/libtailercommon.a \
	tailer/libtailerpp.a \
	yajl/libyajl.a \
	yajlpp/libyajlpp.a \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	$(LIBARCHIVE_LIBS) \
	$(LIBCURL)

dist_noinst_DATA = \
	alpha-release.sh \
	ansi-palette.json \
	$(BUILTIN_LNAVSCRIPTS) \
	$(BUILTIN_SHSCRIPTS) \
	$(CONFIG_FILES) \
	$(FORMAT_FILES) \
	xterm-palette.json

noinst_HEADERS = \
	all_logs_vtab.hh \
	ansi_scrubber.hh \
	archive_manager.hh \
	archive_manager.cfg.hh \
	attr_line.hh \
	auto_fd.hh \
	auto_mem.hh \
	big_array.hh \
	bin2c.hh \
	bookmarks.hh \
	bottom_status_source.hh \
	bound_tags.hh \
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
	file_collection.hh \
	file_format.hh \
	file_vtab.cfg.hh \
	files_sub_source.hh \
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
	input_dispatcher.hh \
	k_merge_tree.h \
	line_buffer.hh \
	listview_curses.hh \
	lnav.hh \
	lnav_commands.hh \
	lnav_config.hh \
	lnav_config_fwd.hh \
	lnav_util.hh \
	log_accel.hh \
	log_actions.hh \
    log_data_helper.hh \
    log_data_table.hh \
	log_format.hh \
	log_format_ext.hh \
	log_format_fwd.hh \
	log_format_loader.hh \
	log_gutter_source.hh \
	log_level.hh \
	log_level_re.re \
	log_search_table.hh \
	logfile.hh \
	logfile.cfg.hh \
	logfile_fwd.hh \
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
	safe/accessmode.h \
	safe/defaulttypes.h \
	safe/mutableref.h \
	safe/safe.h \
	sequence_matcher.hh \
	sequence_sink.hh \
	service_tags.hh \
	session_data.hh \
	shared_buffer.hh \
	shlex.hh \
	simdutf8check.h \
	spectro_source.hh \
	sql_util.hh \
	sqlite-extension-func.hh \
	styling.hh \
	statusview_curses.hh \
	string_attr_type.hh \
	strong_int.hh \
	sysclip.hh \
	termios_guard.hh \
	term_extra.hh \
	text_format.hh \
	textfile_highlighters.hh \
	textfile_sub_source.hh \
	textview_curses.hh \
	textview_curses_fwd.hh \
	time_T.hh \
	timer.hh \
	top_status_source.hh \
	unique_path.hh \
	url_loader.hh \
	view_curses.hh \
	view_helpers.hh \
	views_vtab.hh \
	vis_line.hh \
	vt52_curses.hh \
	vtab_module.hh \
	vtab_module_json.hh \
	log_vtab_impl.hh \
	log_format_impls.cc \
	xml_util.hh \
	xpath_vtab.hh \
	xterm_mouse.hh \
	spookyhash/SpookyV2.h \
	ghc/filesystem.hpp \
	ghc/fs_fwd.hpp \
	ghc/fs_impl.hpp \
	ghc/fs_std.hpp \
	ghc/fs_std_fwd.hpp \
	ghc/fs_std_impl.hpp \
    ww898/cp_utf8.hpp

nodist_libdiag_a_SOURCES = \
	$(LNAV_BUILT_FILES)

THIRD_PARTY_SRCS = \
    third-party/backward-cpp/backward.hpp \
	third-party/sqlite/ext/dbdump.c \
	third-party/sqlite/ext/series.c

libdiag_a_SOURCES = \
	$(THIRD_PARTY_SRCS) \
	all_logs_vtab.cc \
	ansi_scrubber.cc \
	archive_manager.cc \
	attr_line.cc \
	bookmarks.cc \
	bottom_status_source.cc \
	collation-functions.cc \
	column_namer.cc \
	command_executor.cc \
	curl_looper.cc \
	data_scanner.cc \
	data_scanner_re.cc \
	db_sub_source.cc \
	elem_to_json.cc \
	environ_vtab.cc \
	extension-functions.cc \
	field_overlay_source.cc \
	file_collection.cc \
	file_format.cc \
	files_sub_source.cc \
	filter_observer.cc \
	filter_status_source.cc \
	filter_sub_source.cc \
	fstat_vtab.cc \
    fs-extension-functions.cc \
    fts_fuzzy_match.cc \
	grep_proc.cc \
	help_text.cc \
	help_text_formatter.cc \
	highlighter.cc \
	hist_source.cc \
	hotkeys.cc \
	input_dispatcher.cc \
	json-extension-functions.cc \
	line_buffer.cc \
	listview_curses.cc \
	lnav_commands.cc \
	lnav_config.cc \
	lnav_util.cc \
	log_accel.cc \
	log_actions.cc \
	log_data_helper.cc \
	log_data_table.cc \
	log_format.cc \
	log_format_loader.cc \
	log_level.cc \
	log_level_re.cc \
	log_search_table.cc \
	logfile.cc \
	logfile_sub_source.cc \
	network-extension-functions.cc \
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
	spectro_source.cc \
	sqlite-extension-func.cc \
	statusview_curses.cc \
	string-extension-functions.cc \
	string_attr_type.cc \
	styling.cc \
	text_format.cc \
	textfile_sub_source.cc \
	timer.cc \
	piper_proc.cc \
	sql_commands.cc \
	sql_util.cc \
	state-extension-functions.cc \
	sysclip.cc \
	textfile_highlighters.cc \
	textview_curses.cc \
	time-extension-functions.cc \
	top_status_source.cc \
	unique_path.cc \
	view_curses.cc \
	view_helpers.cc \
	views_vtab.cc \
	vt52_curses.cc \
	vtab_module.cc \
	log_vtab_impl.cc \
	xml_util.cc \
	xpath_vtab.cc \
	xterm_mouse.cc \
	spookyhash/SpookyV2.cpp

PLUGIN_SRCS = \
	file_vtab.cc

lnav.$(OBJEXT): help-txt.h init-sql.h

lnav_SOURCES = lnav.cc $(PLUGIN_SRCS)

lnav_test_SOURCES = lnav.cc test_override.c $(PLUGIN_SRCS)

ptimec$(BUILD_EXEEXT): ptimec.c
	$(AM_V_CC) $(CC_FOR_BUILD) $(CPPFLAGS) $(LDFLAGS) -g3 -o $@ $?

if HAVE_RE2C
RE2C_FILES = data_scanner_re.cc log_level_re.cc
endif

EXTRA_DIST = \
	ptimec.c

CLEANFILES = \
	ptimec$(BUILD_EXEEXT)

DISTCLEANFILES = \
    $(LNAV_BUILT_FILES) \
	$(RE2C_FILES)

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))

if !DISABLE_DOCUMENTATION
all-local: lnav
	if test -w $(srcdir)/internals; then \
		env DUMP_INTERNALS_DIR=$(srcdir)/internals DUMP_CRASH=1 ./lnav Makefile; \
		mv $(srcdir)/internals/*.schema.json $(top_srcdir)/docs/schemas; \
	fi
endif

install-exec-hook:
	bash $(srcdir)/alpha-release.sh
