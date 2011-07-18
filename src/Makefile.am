
bin_PROGRAMS = lnav bin2c

noinst_LIBRARIES = libdiag.a

help.c: help.txt bin2c
	./bin2c -z -c $< $@

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
	-lpcrecpp

noinst_HEADERS = \
	auto_fd.hh \
	auto_mem.hh \
	auto_temp_file.hh \
	bookmarks.hh \
	bottom_status_source.hh \
	byte_array.hh \
	data_scanner.hh \
	data_parser.hh \
	db_sub_source.hh \
	grep_proc.hh \
	help.hh \
	help.txt \
	hist_source.hh \
	line_buffer.hh \
	listview_curses.hh \
	lnav_util.hh \
	log_format.hh \
	logfile.hh \
	logfile_sub_source.hh \
	pcrepp.hh \
	piper_proc.hh \
	readline_curses.hh \
	sequence_matcher.hh \
	sequence_sink.hh \
	statusview_curses.hh \
	strong_int.hh \
	termios_guard.hh \
	textfile_sub_source.hh \
	textview_curses.hh \
	time_T.hh \
	top_status_source.hh \
	view_curses.hh \
	vt52_curses.hh \
	log_vtab_impl.hh \
	log_format_impls.cc

libdiag_a_SOURCES = \
	bookmarks.cc \
	grep_proc.cc \
	hist_source.cc \
	line_buffer.cc \
	listview_curses.cc \
	log_format.cc \
	logfile.cc \
	logfile_sub_source.cc \
	data_scanner.cc \
	data_parser.cc \
	readline_curses.cc \
	sequence_matcher.cc \
	statusview_curses.cc \
	piper_proc.cc \
	textview_curses.cc \
	view_curses.cc \
	vt52_curses.cc \
	log_vtab_impl.cc

lnav_SOURCES = lnav.cc $(HELP_SRC)
lnav_LDADD = help.o $(LDADD)

bin2c_SOURCES = bin2c.c

DISTCLEANFILES = help.c

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))
