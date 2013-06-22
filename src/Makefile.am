
bin_PROGRAMS = lnav

noinst_PROGRAMS = bin2c lnav-test

noinst_LIBRARIES = libdiag.a

help.c: $(srcdir)/help.txt bin2c
	./bin2c -z -c $(srcdir)/help.txt $@

init.sql: $(srcdir)/init_sql.py
	$(PYTHON) $(srcdir)/init_sql.py > $@

init-sql.c: $(srcdir)/init.sql bin2c
	./bin2c -z -c $(srcdir)/init.sql $@

AM_LDFLAGS = \
	$(SQLITE3_LDFLAGS) \
	$(PCRE_LDFLAGS) \
	-pthread \
	$(STATIC_LDFLAGS)

AM_CPPFLAGS = \
	-Wall \
	$(SQLITE3_CFLAGS)

LDADD = \
	libdiag.a \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(SQLITE3_LIBS) \
	-lpcrecpp \
	-lcrypto

noinst_HEADERS = \
	ansi_scrubber.hh \
	auto_fd.hh \
	auto_mem.hh \
	bookmarks.hh \
	bottom_status_source.hh \
	byte_array.hh \
	column_namer.hh \
	data_scanner.hh \
	data_parser.hh \
	db_sub_source.hh \
	grapher.hh \
	grep_highlighter.hh \
	grep_proc.hh \
	help.hh \
	help.txt \
	hist_source.hh \
	init.sql \
	init-sql.hh \
	line_buffer.hh \
	listview_curses.hh \
	lnav.hh \
	lnav_commands.hh \
	lnav_config.hh \
	lnav_util.hh \
    log_data_table.hh \
	log_format.hh \
	logfile.hh \
	logfile_sub_source.hh \
	pcrepp.hh \
	piper_proc.hh \
	readline_curses.hh \
	sequence_matcher.hh \
	sequence_sink.hh \
	session_data.hh \
	sql_util.hh \
	sqlite-extension-func.h \
    status_controllers.hh \
	statusview_curses.hh \
	strnatcmp.h \
	strong_int.hh \
	termios_guard.hh \
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
	yajl/yajl_version.h

libdiag_a_SOURCES = \
	ansi_scrubber.cc \
	bookmarks.cc \
	collation-functions.cc \
	datetime-extension-functions.cc \
	extension-functions.c \
    fs-extension-functions.cc \
	grep_proc.cc \
	hist_source.cc \
	line_buffer.cc \
	listview_curses.cc \
	lnav_commands.cc \
	lnav_config.cc \
	lnav_util.cc \
	log_format.cc \
	logfile.cc \
	logfile_sub_source.cc \
	network-extension-functions.cc \
	data_scanner.cc \
	data_parser.cc \
	readline_curses.cc \
	session_data.cc \
	sequence_matcher.cc \
	sqlite-extension-func.c \
	statusview_curses.cc \
	string-extension-functions.cc \
	pcrepp.cc \
	piper_proc.cc \
	sql_util.cc \
	strnatcmp.c \
	textview_curses.cc \
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
	yajl/yajl_version.c

dist_noinst_SCRIPTS = \
	init_sql.py

lnav_SOURCES = lnav.cc $(HELP_SRC)
lnav_LDADD = help.o init-sql.o $(LDADD)

lnav_test_SOURCES = lnav.cc $(HELP_SRC) test_override.cc
lnav_test_LDADD = help.o init-sql.o $(LDADD)

bin2c_SOURCES = bin2c.c

DISTCLEANFILES = help.c init-sql.c

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))
