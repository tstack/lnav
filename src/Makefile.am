
bin_PROGRAMS = lnav

noinst_LIBRARIES = libdiag.a

if HAVE_OBJCOPY
help.txt.term: help.txt
	cat $< > $@
	/bin/echo -en '\000' >> $@

help.o: help.txt.term
	objcopy -I binary -O elf64-x86-64 -Bi386 \
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
	$(SQLITE3_LDFLAGS)

AM_CPPFLAGS = \
	$(SQLITE3_CFLAGS) \
	$(SOCI_CXXFLAGS)

LDADD = \
	libdiag.a \
	$(READLINE_LIBS) \
	$(CURSES_LIB) \
	$(HELP_O) \
	$(SOCI_LIBS) \
	/usr/lib/libsqlite3.a \
	-ldl \
	-lz \
	-lpcrecpp

noinst_HEADERS = \
	bookmarks.hh \
	grep_proc.hh \
	help.hh \
	hist_source.hh \
	line_buffer.hh \
	listview_curses.hh \
	log_format.hh \
	logfile.hh \
	logfile_sub_source.hh \
	readline_curses.hh \
	statusview_curses.hh \
	piper_proc.hh \
	textview_curses.hh \
	view_curses.hh \
	vt52_curses.hh \
	log_vtab_impl.hh

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

uncrusty:
	(cd $(srcdir) && uncrustify -c ../lnav.cfg --replace $(SOURCES) \
		$(HEADERS))
