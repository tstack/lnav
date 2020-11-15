
AM_CPPFLAGS = \
    -Wall \
    -I$(top_srcdir)/src/ \
    -I$(top_srcdir)/src/fmtlib

noinst_LIBRARIES = libbase.a

noinst_HEADERS = \
    enum_util.hh \
    file_range.hh \
    func_util.hh \
    future_util.hh \
	humanize.hh \
	intern_string.hh \
    is_utf8.hh \
    lnav_log.hh \
    opt_util.hh \
    result.h \
    string_util.hh

libbase_a_SOURCES = \
	humanize.cc \
	intern_string.cc \
    is_utf8.cc \
    lnav_log.cc \
    string_util.cc
