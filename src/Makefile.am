
include $(top_srcdir)/aminclude_static.am

CXXFLAGS =

SUBDIRS = \
    fmtlib \
    third-party/base64/lib \
    third-party/date/src \
    third-party/scnlib/src \
    pcrepp \
    base \
    tailer \
    pugixml \
    yajl \
    yajlpp \
    formats/logfmt \
    .

bin_PROGRAMS = lnav

noinst_PROGRAMS = lnav-test

noinst_LIBRARIES = libdiag.a libdatascanner.a

PTIME_V = $(PTIME_V_@AM_V@)
PTIME_V_ = $(PTIME_V_@AM_DEFAULT_V@)
PTIME_V_0 = @echo "  TIMEFMT " $@;

BIN2C_V = $(BIN2C_V_@AM_V@)
BIN2C_V_ = $(BIN2C_V_@AM_DEFAULT_V@)
BIN2C_V_0 = @echo "  BIN2C   " $@;

RE2C_V = $(RE2C_V_@AM_V@)
RE2C_V_ = $(RE2C_V_@AM_DEFAULT_V@)
RE2C_V_0 = @echo "  RE2C   " $@;

BIN2C_PATH = ../tools/bin2c$(BUILD_EXEEXT)

include formats/formats.am

default-formats.cc: $(BIN2C_PATH) $(FORMAT_FILES)
	$(BIN2C_V)$(BIN2C_PATH) -n lnav_format_json default-formats $(FORMAT_FILES)

include keymaps/keymaps.am
include prql/prql.am
include themes/themes.am

CONFIG_FILES = \
    $(srcdir)/root-config.json \
    $(KEYMAP_FILES) \
    $(THEME_FILES) \
    $()

default-config.cc: $(BIN2C_PATH) $(CONFIG_FILES)
	$(BIN2C_V)$(BIN2C_PATH) -n lnav_config_json default-config $(CONFIG_FILES)

prql-modules.cc: $(BIN2C_PATH) $(PRQL_FILES)
	$(BIN2C_V)$(BIN2C_PATH) -n lnav_prql_modules prql-modules $(PRQL_FILES)

include scripts/scripts.am

builtin-scripts.cc: $(BIN2C_PATH) $(BUILTIN_LNAVSCRIPTS)
	$(BIN2C_V)$(BIN2C_PATH) -n lnav_scripts builtin-scripts $(BUILTIN_LNAVSCRIPTS)

builtin-sh-scripts.cc: $(BIN2C_PATH) $(BUILTIN_SHSCRIPTS)
	$(BIN2C_V)$(BIN2C_PATH) -n lnav_sh_scripts builtin-sh-scripts $(BUILTIN_SHSCRIPTS)

%-sh.cc: $(srcdir)/%.sh $(BIN2C_PATH)
	$(BIN2C_V)$(BIN2C_PATH) $(*)-sh $<

%-txt.cc: $(srcdir)/%.txt $(BIN2C_PATH)
	$(BIN2C_V)$(BIN2C_PATH) $(*)-txt $<

%-md.cc: $(srcdir)/%.md $(BIN2C_PATH)
	$(BIN2C_V)$(BIN2C_PATH) $(*)-md $<

%-sql.cc: $(srcdir)/%.sql $(BIN2C_PATH)
	$(BIN2C_V)$(BIN2C_PATH) $(*)-sql $<

%-prql.cc: $(srcdir)/%.prql $(BIN2C_PATH)
	$(BIN2C_V)$(BIN2C_PATH) $(*)-prql $<

%-lnav.cc: $(srcdir)/%.lnav $(BIN2C_PATH)
	$(BIN2C_V)$(BIN2C_PATH) $(*)-lnav $<

%-json.cc: $(srcdir)/%.json $(BIN2C_PATH)
	$(BIN2C_V)$(BIN2C_PATH) $(*)-json $<

include time_formats.am

time_fmts.cc: ptimec$(BUILD_EXEEXT)
	$(PTIME_V)./ptimec$(BUILD_EXEEXT) $(TIME_FORMATS) > $@

if HAVE_RE2C
%.cc: %.re
	$(RE2C_V)$(RE2C_CMD) --bit-vectors -W -8 -c -o $@ $<
	$(REC2_V)test $@ -ef $(srcdir)/$*.cc || cp $@ $(srcdir)/$*.cc
endif

LNAV_BUILT_FILES = \
    animals-json.cc \
    ansi-palette-json.cc \
    builtin-scripts.cc \
    builtin-sh-scripts.cc \
    css-color-names-json.cc \
    default-config.cc \
    default-formats.cc \
    diseases-json.cc \
    emojis-json.cc \
    words-json.cc \
    help-md.cc \
    init-sql.cc \
    prql-modules.cc \
    time_fmts.cc \
    xml-entities-json.cc \
    xterm-palette-json.cc

BUILT_SOURCES = $(LNAV_BUILT_FILES)

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS) $(USER_CXXFLAGS)

if HAVE_CARGO
RUST_DEPS_CPPFLAGS = -DHAVE_RUST_DEPS=1
PRQLC_DIR = third-party/prqlc-c/target
RUST_DEPS_LIBS = $(PRQLC_DIR)/release/libprqlc_c.a

$(RUST_DEPS_LIBS): $(srcdir)/third-party/prqlc-c/src/lib.rs $(srcdir)/third-party/prqlc-c/Cargo.toml
	mkdir -p $(PRQLC_DIR)
	env CARGO_TARGET_DIR=third-party/prqlc-c/target $(CARGO_CMD) build --manifest-path \
	    $(srcdir)/third-party/prqlc-c/Cargo.toml --package prqlc-c --release

else
RUST_DEPS =
RUST_DEPS_CPPFLAGS =
RUST_DEPS_LIBS =
endif

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
	-I$(srcdir)/third-party/base64/include \
	-I$(srcdir)/third-party/date/include \
	-I$(srcdir)/third-party/prqlc-c \
	-I$(srcdir)/third-party/rapidyaml \
	-I$(top_srcdir)/src/third-party/scnlib/include \
	-Wall \
	$(CODE_COVERAGE_CPPFLAGS) \
	$(LIBARCHIVE_CFLAGS) \
	$(READLINE_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(PCRE_CFLAGS) \
	$(LIBCURL_CPPFLAGS) \
	$(RUST_DEPS_CPPFLAGS)

LDADD = \
	libdiag.a \
	libdatascanner.a \
	base/libbase.a \
	formats/logfmt/liblogfmt.a \
	fmtlib/libcppfmt.a \
	third-party/date/src/libdatepp.a \
	third-party/scnlib/src/libscnlib.a \
	pcrepp/libpcrepp.a \
	pugixml/libpugixml.a \
	tailer/libtailerservice.a \
	tailer/libtailercommon.a \
	tailer/libtailerpp.a \
	yajl/libyajl.a \
	yajlpp/libyajlpp.a \
	third-party/base64/lib/libbase64.a \
    $(RUST_DEPS_LIBS) \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	$(LIBARCHIVE_LIBS) \
	$(LIBCURL)

# emojis.json is from https://gist.github.com/oliveratgithub/0bf11a9aff0d6da7b46f1490f86a71eb/
# xml-entities.json is from https://html.spec.whatwg.org/entities.json
# css-color-names.json is from https://github.com/bahamas10/css-color-names/blob/master/css-color-names.json

dist_noinst_DATA = \
	alpha-release.sh \
	animals.json \
	ansi-palette.json \
	css-color-names.json \
	diseases.json \
	emojis.json \
	$(BUILTIN_LNAVSCRIPTS) \
	$(BUILTIN_SHSCRIPTS) \
	$(CONFIG_FILES) \
	$(FORMAT_FILES) \
	$(PRQL_FILES) \
	words.json \
	xml-entities.json \
	xterm-palette.json

noinst_HEADERS = \
    third-party/md4c/md4c.h \
    third-party/prqlc-c/prqlc.h \
    third-party/prqlc-c/prqlc.hpp \
    third-party/prqlc-c/prqlc.cxx.hh \
    third-party/rapidyaml/ryml_all.hpp \
	all_logs_vtab.hh \
	archive_manager.hh \
	archive_manager.cfg.hh \
	big_array.hh \
	bin2c.hh \
	bookmarks.hh \
	bookmarks.json.hh \
	bottom_status_source.hh \
	bound_tags.hh \
	breadcrumb.hh \
	breadcrumb_curses.hh \
	byte_array.hh \
	column_namer.hh \
	command_executor.hh \
	curl_looper.hh \
	data_scanner.hh \
	data_scanner_re.re \
	data_parser.hh \
	db_sub_source.hh \
	doc_status_source.hh \
	document.sections.hh \
	dump_internals.hh \
	elem_to_json.hh \
	environ_vtab.hh \
	field_overlay_source.hh \
	file_collection.hh \
	file_converter_manager.hh \
	file_format.hh \
	file_options.hh \
	file_vtab.cfg.hh \
	files_sub_source.hh \
	filter_observer.hh \
	filter_status_source.hh \
	filter_sub_source.hh \
	fstat_vtab.hh \
	fts_fuzzy_match.hh \
	gantt_source.hh \
	gantt_status_source.hh \
	grep_highlighter.hh \
	grep_proc.hh \
	hasher.hh \
	help.md \
	help.txt \
	help_text.hh \
	help_text_formatter.hh \
	highlighter.hh \
	hist_source.hh \
	hotkeys.hh \
	init.sql \
	input_dispatcher.hh \
	itertools.similar.hh \
	k_merge_tree.h \
	line_buffer.hh \
	listview_curses.hh \
	lnav.hh \
	lnav.events.hh \
	lnav.indexing.hh \
	lnav.management_cli.hh \
	lnav_commands.hh \
	lnav_config.hh \
	lnav_config_fwd.hh \
	lnav_util.hh \
	log.annotate.hh \
	log.annotate.cfg.hh \
	log.watch.hh \
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
	log_search_table_fwd.hh \
	logfile.hh \
	logfile.cfg.hh \
	logfile_fwd.hh \
	logfile_sub_source.hh \
	logfile_sub_source.cfg.hh \
	mapbox/recursive_wrapper.hpp \
	mapbox/variant.hpp \
	mapbox/variant_io.hpp \
	mapbox/variant_visitor.hpp \
	md2attr_line.hh \
	md4cpp.hh \
	piper.looper.hh \
	piper.looper.cfg.hh \
	plain_text_source.hh \
	pollable.hh \
	pretty_printer.hh \
	preview_status_source.hh \
	ptimec.hh \
	readline_callbacks.hh \
	readline_context.hh \
	readline_curses.hh \
	readline_highlighters.hh \
	readline_possibilities.hh \
	regex101.client.hh \
	regex101.import.hh \
	regexp_vtab.hh \
	relative_time.hh \
	ring_span.hh \
	safe/accessmode.h \
	safe/defaulttypes.h \
	safe/mutableref.h \
	safe/safe.h \
	service_tags.hh \
	session.export.hh \
	session_data.hh \
	shared_buffer.hh \
	shlex.hh \
	shlex.resolver.hh \
	simdutf8check.h \
	spectro_impls.hh \
	spectro_source.hh \
	sqlitepp.hh \
	sqlitepp.client.hh \
	sql_execute.hh \
	sql_help.hh \
	sql_util.hh \
	sqlite-extension-func.hh \
	static_file_vtab.hh \
	styling.hh \
	statusview_curses.hh \
	strong_int.hh \
	sysclip.hh \
	sysclip.cfg.hh \
	termios_guard.hh \
	term_extra.hh \
	text_anonymizer.hh \
	text_format.hh \
	text_overlay_menu.hh \
	textfile_highlighters.hh \
	textfile_sub_source.hh \
	textview_curses.hh \
	textview_curses_fwd.hh \
	time_T.hh \
	timer.hh \
	top_status_source.hh \
	top_status_source.cfg.hh \
	unique_path.hh \
	url_handler.cfg.hh \
	url_loader.hh \
	view_curses.hh \
	view_helpers.hh \
	view_helpers.crumbs.hh \
	view_helpers.examples.hh \
	view_helpers.hist.hh \
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
    third-party/ArenaAlloc/arenaalloc.h \
    third-party/ArenaAlloc/arenaallocimpl.h \
    third-party/ArenaAlloc/recyclealloc.h \
    third-party/backward-cpp/backward.hpp \
    third-party/CLI/StringTools.hpp \
    third-party/CLI/App.hpp \
    third-party/CLI/Macros.hpp \
    third-party/CLI/Option.hpp \
    third-party/CLI/Config.hpp \
    third-party/CLI/CLI.hpp \
    third-party/CLI/Formatter.hpp \
    third-party/CLI/Error.hpp \
    third-party/CLI/Version.hpp \
    third-party/CLI/Timer.hpp \
    third-party/CLI/FormatterFwd.hpp \
    third-party/CLI/Validators.hpp \
    third-party/CLI/Split.hpp \
    third-party/CLI/TypeTools.hpp \
    third-party/CLI/ConfigFwd.hpp \
    third-party/doctest-root/doctest/doctest.h \
    third-party/intervaltree/IntervalTree.h \
    third-party/md4c/md4c.c \
    third-party/prqlc-c/prqlc.cxx.cc \
    third-party/robin_hood/robin_hood.h \
	third-party/sqlite/ext/dbdump.c \
	third-party/sqlite/ext/series.c

libdatascanner_a_SOURCES = \
	data_scanner.cc \
	data_scanner_re.cc

libdiag_a_SOURCES = \
	$(THIRD_PARTY_SRCS) \
	all_logs_vtab.cc \
	archive_manager.cc \
	bookmarks.cc \
	bottom_status_source.cc \
	breadcrumb_curses.cc \
	collation-functions.cc \
	column_namer.cc \
	command_executor.cc \
	curl_looper.cc \
	db_sub_source.cc \
	document.sections.cc \
	dump_internals.cc \
	elem_to_json.cc \
	environ_vtab.cc \
	extension-functions.cc \
	field_overlay_source.cc \
	file_collection.cc \
	file_converter_manager.cc \
	file_format.cc \
	file_options.cc \
	files_sub_source.cc \
	filter_observer.cc \
	filter_status_source.cc \
	filter_sub_source.cc \
	fstat_vtab.cc \
    fs-extension-functions.cc \
    fts_fuzzy_match.cc \
    gantt_source.cc \
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
	log.annotate.cc \
	log.watch.cc \
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
	md2attr_line.cc \
	md4cpp.cc \
	network-extension-functions.cc \
	data_parser.cc \
	piper.looper.cc \
	plain_text_source.cc \
	pollable.cc \
	pretty_printer.cc \
	ptimec_rt.cc \
	readline_callbacks.cc \
	readline_curses.cc \
	readline_highlighters.cc \
	readline_possibilities.cc \
	regex101.client.cc \
	regex101.import.cc \
	regexp_vtab.cc \
	relative_time.cc \
	session.export.cc \
	session_data.cc \
	shared_buffer.cc \
	shlex.cc \
	spectro_impls.cc \
	spectro_source.cc \
	sqlitepp.cc \
	sqlite-extension-func.cc \
	static_file_vtab.cc \
	statusview_curses.cc \
	string-extension-functions.cc \
	styling.cc \
	text_anonymizer.cc \
	text_format.cc \
	text_overlay_menu.cc \
	textfile_sub_source.cc \
	timer.cc \
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
	yaml-extension-functions.cc \
	spookyhash/SpookyV2.cpp

PLUGIN_SRCS = \
	file_vtab.cc

lnav_SOURCES = \
    lnav.cc \
    lnav.events.cc \
    lnav.indexing.cc \
    lnav.management_cli.cc \
    $(PLUGIN_SRCS)

lnav_test_SOURCES = \
    lnav.cc \
    lnav.events.cc \
    lnav.indexing.cc \
    lnav.management_cli.cc \
    test_override.c \
    $(PLUGIN_SRCS)

if CROSS_COMPILING
ptimec$(BUILD_EXEEXT): ptimec.c
	$(AM_V_CC) $(CC_FOR_BUILD) $(CPPFLAGS_FOR_BUILD) $(LDFLAGS_FOR_BUILD) -g3 -o $@ $? -fPIE
else
ptimec$(BUILD_EXEEXT): ptimec.c
	$(AM_V_CC) $(CC) $(CPPFLAGS) $(LDFLAGS) -g3 -o $@ $? -fPIE
endif

if HAVE_RE2C
RE2C_FILES = data_scanner_re.cc log_level_re.cc
endif

EXTRA_DIST = \
    third-party/prqlc-c/Cargo.lock \
    third-party/prqlc-c/Cargo.toml \
    third-party/prqlc-c/cbindgen.toml \
    third-party/prqlc-c/src/lib.rs \
	ptimec.c

CLEANFILES = \
	ptimec$(BUILD_EXEEXT)

DISTCLEANFILES = \
    $(LNAV_BUILT_FILES) \
    animals-json.h \
    ansi-palette-json.h \
    builtin-scripts.h \
    builtin-sh-scripts.h \
    css-color-names-json.h \
    default-config.h \
    default-formats.h \
    diseases-json.h \
    emojis-json.h \
    words-json.h \
    help-md.h \
    init-sql.h \
    prql-modules.h \
    time_fmts.h \
    xml-entities-json.h \
    xterm-palette-json.h \
	$(RE2C_FILES)

if HAVE_CARGO
clean-local:
	env CARGO_TARGET_DIR=third-party/prqlc-c/target $(CARGO_CMD) clean --manifest-path \
    	    $(srcdir)/third-party/prqlc-c/Cargo.toml
endif

distclean-local:
	$(RM_V)rm -rf *.dSYM

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))

if !CROSS_COMPILING
all-local: $(LNAV_BUILT_FILES) lnav $(RUST_DEPS)
	if test -w $(srcdir)/internals; then \
		env DUMP_INTERNALS_DIR=$(srcdir)/internals DUMP_CRASH=1 ./lnav Makefile; \
		mv $(srcdir)/internals/*.schema.json $(top_srcdir)/docs/schemas; \
	fi
else
all-local: $(LNAV_BUILT_FILES) $(RUST_DEPS)
endif

check-local:
if HAVE_CHECK_JSONSCHEMA
	$(CHECK_JSONSCHEMA) --schemafile $(top_srcdir)/docs/schemas/format-v1.schema.json $(FORMAT_FILES)
	$(CHECK_JSONSCHEMA) --schemafile $(top_srcdir)/docs/schemas/config-v1.schema.json $(CONFIG_FILES)
endif

install-exec-hook:
	bash $(srcdir)/alpha-release.sh
