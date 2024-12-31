
noinst_HEADERS = \
        ../include/scn/fwd.h \
        ../include/scn/ranges.h \
        ../include/scn/xchar.h \
        ../include/scn/chrono.h \
        ../include/scn/regex.h \
        ../include/scn/istream.h \
        ../include/scn/scan.h \
        scn/impl.h \
        deps/fast_float/single_include/fast_float/fast_float.h

noinst_LIBRARIES = libscnlib.a

AM_CPPFLAGS = -I$(srcdir)/../include -I$(srcdir)/deps/fast_float/single_include

libscnlib_a_SOURCES = \
    scn/impl.cpp
