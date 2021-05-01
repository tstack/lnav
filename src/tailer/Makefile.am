
include $(top_srcdir)/aminclude_static.am

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS)

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = \
    libtailercommon.a \
    libtailerpp.a \
    libtailerservice.a

noinst_HEADERS = \
    sha-256.h \
    tailer.h \
    tailer.looper.hh \
    tailerpp.hh

libtailercommon_a_SOURCES = \
    sha-256.c \
    tailer.c

libtailerpp_a_CPPFLAGS = \
    -I$(srcdir)/.. \
    -I$(srcdir)/../fmtlib

libtailerpp_a_SOURCES = \
    tailerpp.cc

tailerbin.h tailerbin.cc: tailer ../tools/bin2c$(BUILD_EXEEXT)
	../tools/bin2c$(BUILD_EXEEXT) -n tailer_bin $(*) $<

libtailerservice_a_CPPFLAGS = \
    -I$(srcdir)/.. \
    -I$(srcdir)/../fmtlib

libtailerservice_a_LIBADD = \
    libtailercommon.a \
    libtailerpp.a

libtailerservice_a_SOURCES = \
    tailerbin.cc \
    tailer.looper.cc

noinst_PROGRAMS = \
    drive_tailer \
    tailer

tailer_SOURCES = \
    tailer.main.c

tailer_LDADD = libtailercommon.a

drive_tailer_CPPFLAGS = \
    -I$(srcdir)/.. \
    -I$(srcdir)/../fmtlib

drive_tailer_SOURCES = \
    drive_tailer.cc

drive_tailer_LDADD = \
    libtailercommon.a \
    libtailerpp.a \
    ../base/libbase.a \
    ../fmtlib/libcppfmt.a

DISTCLEANFILES = \
    tailerbin.h \
    tailerbin.cc
