
include $(top_srcdir)/aminclude_static.am

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
    -Wall \
    -I$(top_srcdir)/src/ \
    -I$(top_srcdir)/src/third-party \
    -I$(top_srcdir)/src/fmtlib \
	-I$(top_srcdir)/src/third-party/scnlib/include \
    $(LIBARCHIVE_CFLAGS) \
    $(READLINE_CFLAGS) \
    $(SQLITE3_CFLAGS) \
    $(LIBCURL_CPPFLAGS)

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = liblogfmt.a

noinst_HEADERS = \
    logfmt.parser.hh

liblogfmt_a_SOURCES = \
    logfmt.parser.cc

check_PROGRAMS = \
    logfmt.parser.test

logfmt_parser_test_SOURCES = \
    logfmt.parser.test.cc

logfmt_parser_test_LDADD = \
    liblogfmt.a \
    $(top_builddir)/src/base/libbase.a \
    $(top_builddir)/src/pcrepp/libpcrepp.a \
    $(top_builddir)/src/fmtlib/libcppfmt.a \
    $(top_builddir)/src/third-party/scnlib/src/libscnlib.a \
    -lunistring

TESTS = \
    logfmt.parser.test
