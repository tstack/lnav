
include $(top_srcdir)/aminclude_static.am

TESTS_ENVIRONMENT = $(BASH) $(top_builddir)/TESTS_ENVIRONMENT
LOG_COMPILER = $(BASH) $(top_builddir)/TESTS_ENVIRONMENT

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
    -Wall \
	$(LIBARCHIVE_CFLAGS) \
	$(PCRE_CFLAGS) \
    -I$(top_srcdir)/src/ \
    -I$(top_srcdir)/src/fmtlib \
	-I$(top_srcdir)/src/third-party/date/include \
	-I$(top_srcdir)/src/third-party/scnlib/include

AM_LDFLAGS = \
    $(LIBARCHIVE_LDFLAGS) \
	$(STATIC_LDFLAGS)

AM_LIBS = $(CODE_COVERAGE_LIBS)
AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libyajlpp.a

noinst_HEADERS = \
    json_op.hh \
    json_ptr.hh \
	yajlpp.hh \
	yajlpp_def.hh

libyajlpp_a_SOURCES = \
    json_op.cc \
    json_ptr.cc \
	yajlpp.cc

check_PROGRAMS = \
	drive_json_op \
	drive_json_ptr_walk \
	test_json_ptr \
	test_yajlpp

drive_json_op_SOURCES = drive_json_op.cc

drive_json_ptr_walk_SOURCES = drive_json_ptr_walk.cc

test_json_ptr_SOURCES = test_json_ptr.cc

test_yajlpp_SOURCES = test_yajlpp.cc

LDADD = \
    $(LIBARCHIVE_LIBS) \
    libyajlpp.a \
    $(top_builddir)/src/base/libbase.a \
    $(top_builddir)/src/fmtlib/libcppfmt.a \
    $(top_builddir)/src/third-party/date/src/libdatepp.a \
    $(top_builddir)/src/third-party/scnlib/src/libscnlib.a \
    $(top_builddir)/src/pcrepp/libpcrepp.a \
    $(top_builddir)/src/yajl/libyajl.a \
    -lunistring

dist_noinst_SCRIPTS = \
	test_json_op.sh \
	test_json_ptr_walk.sh

TESTS = \
	test_json_op.sh \
    test_json_ptr \
	test_json_ptr_walk.sh \
    test_yajlpp

DISTCLEANFILES = \
	*.dat \
	*.err \
	*.db \
	*.dpt \
	*.diff \
	*.index \
	*.tmp \
	*.errbak \
	*.tmpbak \
	*.gz \
	*.bz2
