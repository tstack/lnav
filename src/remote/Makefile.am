
include $(top_srcdir)/aminclude_static.am

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
    -Wall \
    -I$(top_srcdir)/src/ \
    -I$(top_srcdir)/src/fmtlib

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libremote.a

noinst_HEADERS = \
    remote.ssh.hh

libremote_a_SOURCES = \
    remote.ssh.cc
