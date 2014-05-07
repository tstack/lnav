
bin_PROGRAMS = lnav

noinst_PROGRAMS = bin2c ptimec lnav-test

noinst_LIBRARIES = libdiag.a

help.c: $(srcdir)/help.txt bin2c
	./bin2c -z -c $(srcdir)/help.txt $@

dump-pid-sh.c: $(srcdir)/dump-pid.sh bin2c
	./bin2c -z -c $(srcdir)/dump-pid.sh $@

default-log-formats-json.c: $(srcdir)/default-log-formats.json bin2c
	./bin2c -z -c $(srcdir)/default-log-formats.json $@

init.sql: $(srcdir)/init_sql.py
	$(PYTHON) $(srcdir)/init_sql.py > $@

init-sql.c: $(srcdir)/init.sql bin2c
	./bin2c -z -c $(srcdir)/init.sql $@

TIME_FORMATS = \
    "%Y-%m-%d %H:%M:%S" \
    "%Y-%m-%d %H:%M" \
    "%Y-%m-%dT%H:%M:%S" \
    "%Y-%m-%dT%H:%M:%SZ" \
    "%Y/%m/%d %H:%M:%S" \
    "%Y/%m/%d %H:%M" \
    "%a %b %d %H:%M:%S %Y" \
    "%a %b %d %H:%M:%S %Z %Y" \
    "%a %b %d %H:%M:%S " \
    "%d/%b/%Y:%H:%M:%S %z" \
    "%b %d %H:%M:%S" \
    "%m/%d/%y %H:%M:%S" \
    "%m%d %H:%M:%S" \
    "%M:%S"

time_fmts.cc: ptimec
	./ptimec $(TIME_FORMATS) > $@

AM_LDFLAGS = \
	$(STATIC_LDFLAGS) \
	$(SQLITE3_LDFLAGS) \
	$(PCRE_LDFLAGS) \
	-pthread

AM_CPPFLAGS = \
	-DSYSCONFDIR='"$(sysconfdir)"' \
	-Wall \
	$(SQLITE3_CFLAGS)

LDADD = \
	libdiag.a \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	-lpcrecpp

dist_noinst_DATA = \
	default-log-formats.json \
	dump-pid.sh

noinst_HEADERS = \
	ansi_scrubber.hh \
	auto_fd.hh \
	auto_mem.hh \
	auto_pid.hh \
	bookmarks.hh \
	bottom_status_source.hh \
	byte_array.hh \
	column_namer.hh \
	data_scanner.hh \
	data_parser.hh \
	default-log-formats-json.hh \
	db_sub_source.hh \
	environ_vtab.hh \
	format-text-files.hh \
	grapher.hh \
	grep_highlighter.hh \
	grep_proc.hh \
	help.hh \
	help.txt \
	hist_source.hh \
	init.sql \
	init-sql.hh \
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
    log_data_helper.hh \
    log_data_table.hh \
	log_format.hh \
	log_format_loader.hh \
	logfile.hh \
	logfile_sub_source.hh \
	pcrepp.hh \
	piper_proc.hh \
	ptimec.hh \
	readline_curses.hh \
	readline_highlighters.hh \
	sequence_matcher.hh \
	sequence_sink.hh \
	session_data.hh \
	shared_buffer.hh \
	sql_util.hh \
	sqlite-extension-func.h \
    status_controllers.hh \
	statusview_curses.hh \
	strnatcmp.h \
	strong_int.hh \
	termios_guard.hh \
	term_extra.hh \
	textfile_sub_source.hh \
	textview_curses.hh \
	time_T.hh \
	top_status_source.hh \
	view_curses.hh \
	vt52_curses.hh \
	log_vtab_impl.hh \
	log_format_impls.cc \
	xterm_mouse.hh \
	yajlpp.hh \
	yajl/api/yajl_common.h \
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
	yajl/yajl_version.h \
	spookyhash/SpookyV2.h

libdiag_a_SOURCES = \
	ansi_scrubber.cc \
	bookmarks.cc \
	collation-functions.cc \
	db_sub_source.cc \
	environ_vtab.cc \
	extension-functions.c \
    fs-extension-functions.cc \
	grep_proc.cc \
	hist_source.cc \
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
	log_format.cc \
	log_format_loader.cc \
	logfile.cc \
	logfile_sub_source.cc \
	network-extension-functions.cc \
	data_scanner.cc \
	data_parser.cc \
	ptimec_rt.cc \
	readline_curses.cc \
	readline_highlighters.cc \
	session_data.cc \
	sequence_matcher.cc \
	shared_buffer.cc \
	sqlite-extension-func.c \
	statusview_curses.cc \
	string-extension-functions.cc \
	pcrepp.cc \
	piper_proc.cc \
	sql_util.cc \
	state-extension-functions.cc \
	strnatcmp.c \
	textview_curses.cc \
	time_fmts.cc \
	view_curses.cc \
	vt52_curses.cc \
	log_vtab_impl.cc \
	xterm_mouse.cc \
	yajlpp.cc \
	yajl/yajl.c \
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
	yajl/yajl_version.c \
	spookyhash/SpookyV2.cpp

dist_noinst_SCRIPTS = \
	init_sql.py

TEXT2C_FILES = \
	dump-pid-sh.o \
	help.o \
	init-sql.o \
	default-log-formats-json.o

lnav_SOURCES = lnav.cc $(HELP_SRC)
lnav_LDADD = \
	$(TEXT2C_FILES) \
	$(LDADD)

lnav_test_SOURCES = lnav.cc $(HELP_SRC) test_override.cc
lnav_test_LDADD = \
	$(TEXT2C_FILES) \
	$(LDADD)

bin2c_SOURCES = bin2c.c
bin2c_LDADD =

ptimec_SOURCES = ptimec.cc
ptimec_LDADD =

DISTCLEANFILES = \
	dump-pid-sh.c \
	help.c \
	init-sql.c \
	default-log-formats-json.c \
    time_fmts.cc

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))
