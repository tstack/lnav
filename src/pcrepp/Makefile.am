
include $(top_srcdir)/aminclude_static.am

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	$(PCRE_CFLAGS) \
	-Wall \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/fmtlib \
	-I$(top_srcdir)/src/third-party/scnlib/include

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libpcrepp.a

noinst_HEADERS = \
	pcre2pp.hh \
	pcre2pp_fwd.hh

libpcrepp_a_SOURCES = \
	pcre2pp.cc

test_pcre2pp_SOURCES = test_pcre2pp.cc
test_pcre2pp_LDADD = \
    libpcrepp.a \
    $(PCRE_LIBS)

check_PROGRAMS = \
	test_pcre2pp

TESTS = \
	test_pcre2pp
