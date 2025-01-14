
include $(top_srcdir)/aminclude_static.am

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
    -Wall \
    -I$(top_srcdir)/src/ \
    -I$(top_srcdir)/src/third-party \
    -I$(top_srcdir)/src/fmtlib \
	-I$(top_srcdir)/src/third-party/date/include \
	-I$(top_srcdir)/src/third-party/notcurses/include \
	-I$(top_srcdir)/src/third-party/scnlib/include \
    $(LIBARCHIVE_CFLAGS) \
    $(READLINE_CFLAGS) \
    $(SQLITE3_CFLAGS) \
    $(PCRE_CFLAGS) \
    $(LIBCURL_CPPFLAGS)

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libbase.a

noinst_HEADERS = \
    ansi_scrubber.hh \
    ansi_vars.hh \
    attr_line.hh \
    attr_line.builder.hh \
    auto_fd.hh \
    auto_mem.hh \
    auto_pid.hh \
    bus.hh \
    color_spaces.hh \
    date_time_scanner.cfg.hh \
    date_time_scanner.hh \
    enum_util.hh \
    file_range.hh \
    from_trait.hh \
    fs_util.hh \
    func_util.hh \
    future_util.hh \
	humanize.hh \
	humanize.network.hh \
	humanize.time.hh \
	injector.hh \
	injector.bind.hh \
	intern_string.hh \
    is_utf8.hh \
    isc.hh \
    itertools.hh \
    itertools.enumerate.hh \
    keycodes.hh \
    line_range.hh \
    lnav_log.hh \
    lnav.console.hh \
    lnav.console.into.hh \
    lnav.gzip.hh \
    log_level_enum.hh \
    lrucache.hpp \
    map_util.hh \
    math_util.hh \
    network.tcp.hh \
    opt_util.hh \
    paths.hh \
    piper.file.hh \
    result.h \
    snippet_highlighters.hh \
    string_attr_type.hh \
    string_util.hh \
    strnatcmp.h \
    time_util.hh \
    types.hh

libbase_a_SOURCES = \
    ansi_scrubber.cc \
    attr_line.cc \
    attr_line.builder.cc \
    auto_fd.cc \
    auto_pid.cc \
    color_spaces.cc \
    date_time_scanner.cc \
    fs_util.cc \
	humanize.cc \
	humanize.network.cc \
	humanize.time.cc \
	intern_string.cc \
    is_utf8.cc \
    isc.cc \
    lnav.console.cc \
    lnav.gzip.cc \
    lnav_log.cc \
    network.tcp.cc \
    paths.cc \
    piper.file.cc \
    snippet_highlighters.cc \
    string_attr_type.cc \
    string_util.cc \
    strnatcmp.c \
    time_util.cc \
	../third-party/xxHash/xxhash.h \
	../third-party/xxHash/xxhash.c

check_PROGRAMS = \
    test_base

test_base_SOURCES = \
    attr_line.tests.cc \
    fs_util.tests.cc \
    humanize.file_size.tests.cc \
    humanize.network.tests.cc \
    humanize.time.tests.cc \
    intern_string.tests.cc \
    lnav.gzip.tests.cc \
    string_util.tests.cc \
    test_base.cc

test_base_LDADD = \
    libbase.a \
    ../fmtlib/libcppfmt.a \
    ../third-party/date/src/libdatepp.a \
    ../third-party/scnlib/src/libscnlib.a \
    ../pcrepp/libpcrepp.a \
    -lunistring

TESTS = \
    test_base
