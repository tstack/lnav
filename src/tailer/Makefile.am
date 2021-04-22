
noinst_LIBRARIES = libtailercommon.a libtailerpp.a

noinst_HEADERS = \
    sha-256.h \
    tailer.h \
    tailerpp.hh

libtailercommon_a_SOURCES = \
    sha-256.c \
    tailer.c

libtailerpp_a_CPPFLAGS = \
    -I$(srcdir)/.. \
    -I$(srcdir)/../fmtlib

libtailerpp_a_SOURCES = \
    tailerpp.cc

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
