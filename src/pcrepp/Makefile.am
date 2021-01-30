
AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-Wall \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/fmtlib

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libpcrepp.a

noinst_HEADERS = \
	pcrepp.hh

libpcrepp_a_SOURCES = \
	pcrepp.cc

test_pcrepp_SOURCES = test_pcrepp.cc
test_pcrepp_LDADD = \
    libpcrepp.a \
    $(top_builddir)/src/base/libbase.a \
    $(PCRE_LIBS)

check_PROGRAMS = \
	test_pcrepp

TESTS = \
	test_pcrepp
