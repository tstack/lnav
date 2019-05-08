
TESTS_ENVIRONMENT = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT
LOG_COMPILER = $(SHELL) $(top_builddir)/TESTS_ENVIRONMENT

AM_CPPFLAGS = \
    -Wall \
    -I$(top_srcdir)/src/ \
    -I$(top_srcdir)/src/fmtlib

AM_LDFLAGS = -pthread

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
    $(top_builddir)/src/base/libbase.a \
    $(top_builddir)/src/fmtlib/libcppfmt.a \
    $(top_builddir)/src/pcrepp/libpcrepp.a \
    $(top_builddir)/src/yajl/libyajl.a \
    libyajlpp.a

dist_noinst_SCRIPTS = \
	test_json_op.sh \
	test_json_ptr_walk.sh

TESTS = \
	test_json_op.sh \
    test_json_ptr \
	test_json_ptr_walk.sh \
    test_yajlpp
