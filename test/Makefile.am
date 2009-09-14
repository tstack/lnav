
TESTS_ENVIRONMENT = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT

AM_CPPFLAGS = \
	-I$(top_srcdir)/src \
	$(SQLITE3_CFLAGS) \
	$(SOCI_CXXFLAGS)

# AM_CFLAGS = -fprofile-arcs -ftest-coverage
# AM_CXXFLAGS = -fprofile-arcs -ftest-coverage

if HAVE_SOCI
SOCI_TESTS =
else
SOCI_TESTS =
endif

check_PROGRAMS = \
	drive_line_buffer \
	drive_grep_proc \
	drive_listview \
	drive_logfile \
	drive_vt52_curses \
	drive_readline_curses \
	slicer \
	scripty \
	test_auto_fd \
	test_auto_mem \
	test_bookmarks \
	test_grep_proc2 \
	$(SOCI_TESTS) \
	test_line_buffer2 \
	test_top_status

AM_LDFLAGS = \
	$(SQLITE3_LDFLAGS)

LDADD = -lz

test_auto_fd_SOURCES = test_auto_fd.cc

test_auto_mem_SOURCES = test_auto_mem.cc

test_bookmarks_SOURCES = test_bookmarks.cc ../src/bookmarks.cc

test_grep_proc2_SOURCES = \
	../src/line_buffer.cc \
	../src/grep_proc.cc \
	test_grep_proc2.cc
test_grep_proc2_LDADD = $(PCRE_LIBS) -lz

test_line_buffer2_SOURCES = \
	../src/line_buffer.cc \
	test_line_buffer2.cc

test_top_status_SOURCES = \
	../src/bookmarks.cc \
	../src/logfile.cc \
	../src/line_buffer.cc \
	../src/log_format.cc \
	../src/logfile_sub_source.cc \
	../src/listview_curses.cc \
	../src/textview_curses.cc \
	../src/view_curses.cc \
	test_top_status.cc
test_top_status_LDADD = $(CURSES_LIB) -lz

drive_line_buffer_SOURCES = \
	../src/line_buffer.cc \
	drive_line_buffer.cc
drive_line_buffer_LDADD = $(CURSES_LIB) -lz

drive_grep_proc_SOURCES = \
	../src/line_buffer.cc \
	../src/grep_proc.cc \
	drive_grep_proc.cc
drive_grep_proc_LDADD = $(PCRE_LIBS) -lz

drive_listview_SOURCES = \
	../src/listview_curses.cc \
	../src/view_curses.cc \
	drive_listview.cc
drive_listview_LDADD = $(CURSES_LIB) -lz

drive_logfile_SOURCES = \
	../src/logfile.cc \
	../src/log_format.cc \
	../src/line_buffer.cc \
	drive_logfile.cc

drive_vt52_curses_SOURCES = \
	../src/vt52_curses.cc \
	drive_vt52_curses.cc
drive_vt52_curses_LDADD = $(CURSES_LIB)

drive_readline_curses_SOURCES = \
	../src/vt52_curses.cc \
	../src/readline_curses.cc \
	drive_readline_curses.cc
drive_readline_curses_LDADD = $(CURSES_LIB) $(READLINE_LIBS)

slicer_SOURCES = \
	../src/line_buffer.cc \
	slicer.cc

scripty_SOURCES = scripty.cc

TESTS = test_bookmarks \
	test_auto_fd \
	test_auto_mem \
	test_line_buffer.sh \
	test_line_buffer2 \
	test_grep_proc.sh \
	test_grep_proc2 \
	$(SOCI_TESTS) \
	test_listview.sh \
	test_logfile.sh \
	test_vt52_curses.sh \
	test_top_status
