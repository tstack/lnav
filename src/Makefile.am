
bin_PROGRAMS = lnav

noinst_LIBRARIES = libdiag.a

if HAVE_OBJ_FORMAT
help.txt.term: help.txt
	cat $< > $@
	/bin/echo -en '\000' >> $@

help.o: help.txt.term
	objcopy -I binary -O $(OBJ_FORMAT) -Bi386 \
		--redefine-sym _binary_help_txt_term_start=help_text_start \
		--redefine-sym _binary_help_txt_term_end=help_text_end \
		--redefine-sym _binary_help_txt_term_size=help_text_size \
		$< $@

HELP_O = help.o
HELP_SRC =
else
HELP_O =
HELP_SRC = help.cc
endif

AM_LDFLAGS = \
	$(SQLITE3_LDFLAGS) \
	-pthread

AM_CPPFLAGS = \
	-Wall \
	$(SQLITE3_CFLAGS)

LDADD = \
	libdiag.a \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(HELP_O) \
	$(SQLITE3_LIBS) \
	-lpcrecpp

noinst_HEADERS = \
	auto_fd.hh \
	auto_mem.hh \
	auto_temp_file.hh \
	bookmarks.hh \
	bottom_status_source.hh \
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
	statusview_curses.hh \
	strong_int.hh \
	termios_guard.hh \
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
	readline_curses.cc \
	statusview_curses.cc \
	piper_proc.cc \
	textview_curses.cc \
	view_curses.cc \
	vt52_curses.cc \
	log_vtab_impl.cc

lnav_SOURCES = lnav.cc $(HELP_SRC)

DISTCLEANFILES = \
	help.txt.term

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))
