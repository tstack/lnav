
noinst_HEADERS = \
    ../include/scn/reader/reader.h \
    ../include/scn/reader/float.h \
    ../include/scn/reader/types.h \
    ../include/scn/reader/int.h \
    ../include/scn/reader/common.h \
    ../include/scn/reader/string.h \
    ../include/scn/ranges/custom_impl.h \
    ../include/scn/ranges/std_impl.h \
    ../include/scn/ranges/ranges.h \
    ../include/scn/ranges/util.h \
    ../include/scn/fwd.h \
    ../include/scn/util/algorithm.h \
    ../include/scn/util/small_vector.h \
    ../include/scn/util/optional.h \
    ../include/scn/util/expected.h \
    ../include/scn/util/array.h \
    ../include/scn/util/unique_ptr.h \
    ../include/scn/util/math.h \
    ../include/scn/util/memory.h \
    ../include/scn/util/span.h \
    ../include/scn/util/meta.h \
    ../include/scn/util/string_view.h \
    ../include/scn/unicode/unicode.h \
    ../include/scn/unicode/common.h \
    ../include/scn/unicode/utf16.h \
    ../include/scn/unicode/utf8.h \
    ../include/scn/all.h \
    ../include/scn/tuple_return/tuple_return.h \
    ../include/scn/tuple_return/util.h \
    ../include/scn/scan/ignore.h \
    ../include/scn/scan/getline.h \
    ../include/scn/scan/list.h \
    ../include/scn/scan/common.h \
    ../include/scn/scan/istream.h \
    ../include/scn/scan/vscan.h \
    ../include/scn/scan/scan.h \
    ../include/scn/tuple_return.h \
    ../include/scn/detail/error.h \
    ../include/scn/detail/fwd.h \
    ../include/scn/detail/range.h \
    ../include/scn/detail/locale.h \
    ../include/scn/detail/config.h \
    ../include/scn/detail/file.h \
    ../include/scn/detail/context.h \
    ../include/scn/detail/result.h \
    ../include/scn/detail/visitor.h \
    ../include/scn/detail/args.h \
    ../include/scn/detail/parse_context.h \
    ../include/scn/detail/vectored.h \
    ../include/scn/scn.h \
    ../include/scn/istream.h \
    deps/fast_float/single_include/fast_float/fast_float.h

noinst_LIBRARIES = libscnlib.a

AM_CPPFLAGS = -I$(srcdir)/../include -I$(srcdir)/deps/fast_float/single_include

libscnlib_a_SOURCES = \
    reader_float.cpp \
    locale.cpp \
    reader_int.cpp \
    file.cpp \
    vscan.cpp
