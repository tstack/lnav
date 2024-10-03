AC_INIT([lnav],[0.12.3],[lnav@googlegroups.com],[lnav],[http://lnav.org])
AC_CONFIG_SRCDIR([src/lnav.cc])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

AC_PREFIX_DEFAULT(/usr)

AC_CANONICAL_HOST
AX_PROG_CC_FOR_BUILD

AX_PTHREAD()
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"
CXX="$PTHREAD_CXX"
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"

AC_LANG(C++)
AX_CXX_COMPILE_STDCXX_17([noext], [mandatory])

dnl abssrcdir is the absolute path to the source base (regardless of where
dnl you are building it)
AS_CASE([x$srcdir],
    [x/*],
    AS_VAR_SET(abssrcdir, $srcdir),
    AS_VAR_SET(abssrcdir, `cd $srcdir; pwd`)
)

AC_SUBST(abssrcdir)

CXX=`echo $CXX | sed -e 's/-std=gnu++11//g'`
CPPFLAGS="$CPPFLAGS -D_ISOC99_SOURCE -D__STDC_LIMIT_MACROS -D_GNU_SOURCE -DUSE_OS_TZDB=1 -DHAS_REMOTE_API=0"

AC_ARG_ENABLE([debug],
              AS_HELP_STRING([--enable-debug],
                             [Compile with symbols]))

AS_VAR_IF([enable_profiling], [yes],
      [CFLAGS=`echo $CFLAGS | sed 's/-O2//g'`
       CXXFLAGS=`echo $CXXFLAGS | sed 's/-O2//g'`],
      [enable_debug=no]dnl
)

AC_ARG_VAR(SFTP_TEST_URL)

AC_PROG_INSTALL
AC_PROG_RANLIB
AM_PROG_AR
AC_PROG_LN_S
AC_PROG_MAKE_SET

AC_PATH_PROG(CARGO_CMD, [cargo])
AC_PATH_PROG(BZIP2_CMD, [bzip2])
AC_PATH_PROG(RE2C_CMD, [re2c])
AM_CONDITIONAL(HAVE_RE2C, test x"$RE2C_CMD" != x"")
AC_PATH_PROG(XZ_CMD, [xz])
AC_PATH_PROG(TSHARK_CMD, [tshark])
AC_PATH_PROG(CHECK_JSONSCHEMA, [check-jsonschema])

AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(size_t)

AC_CHECK_FUNC(mkostemp,
    AC_DEFINE([HAVE_MKOSTEMP], [1],
        [Have the mkostemp() function]
    )
)

AC_STRUCT_TIMEZONE

AC_ARG_ENABLE([static],
              AS_HELP_STRING([--enable-static],
                             [Enable static linking]))

AC_SEARCH_LIBS(openpty, util)
AC_SEARCH_LIBS(gzseek, z, [], [AC_MSG_ERROR([libz required to build])])
AC_SEARCH_LIBS(BZ2_bzopen, bz2,
     AS_VAR_SET(BZIP2_SUPPORT, 1),
     AS_VAR_SET(BZIP2_SUPPORT, 0))
AC_SUBST(BZIP2_SUPPORT)
AC_SEARCH_LIBS(dlopen, dl)
AC_SEARCH_LIBS(backtrace, execinfo)
LIBCURL_CHECK_CONFIG([], [7.23.0], [], [AC_MSG_ERROR([libcurl required to build])], [test x"${enable_static}" = x"yes"])

# Sometimes, curses depends on these libraries being linked in...
AC_ARG_ENABLE([tinfo],
    AS_HELP_STRING([--disable-tinfo],
        [Disable linking with tinfo, enabled by default]),
    [], [enable_tinfo="yes"]
)

AS_IF([test "x${enable_tinfo}" != "xno"],
    [dnl
    AC_MSG_NOTICE([Trying to link with tinfo])
    AC_SEARCH_LIBS(cur_term, [tinfow tinfo],
        AC_MSG_NOTICE([Linking with tinfo]),
        AC_MSG_WARN([libtinfo not found])
    )
    ],
    AC_MSG_NOTICE([Building with tinfo linking disabled])
)

dnl libgpm is required on some systems where there is a misconfigured ncurses
dnl and gpm libraries with interdependencies. This check is not required on OS X.
AS_CASE(["$host_os"],
    [darwin*],
    [],
    AC_SEARCH_LIBS(Gpm_Open, [gpm :libgpm.so.2],
       AS_VAR_SET(HAVE_GPM, "1"),
       AC_MSG_WARN(m4_join([ ],
                   [libgpm not found. If build fails later],
                   [consider installing gpm dev package])dnl
       )
    )
)

AC_CHECK_HEADERS(execinfo.h pty.h util.h zlib.h bzlib.h libutil.h sys/ttydefaults.h)

dnl Experimental SIMD features.
AC_ARG_ENABLE([simd],
    AS_HELP_STRING([--enable-simd], [Try and enable simd optimizations]),
    [
     AS_CASE(["$enable_simd"],
             [no], [enable_simd="no"],
             [yes | ""],[enable_simd="yes"])
     ],
     []
)

AS_IF([test "x$enable_simd" = "xyes"], [
       AC_CHECK_HEADERS(x86intrin.h)
])


AC_ARG_WITH([system_doctest],
    AS_HELP_STRING(
        [--with-system-doctest],
        [Use the system provided doctest library rather than the bundled one]
    ),
    [], []
)

AS_IF([test "x$with_system_doctest" = "xyes"], [
    AC_CHECK_HEADERS(doctest/doctest.h)
    AS_IF([test "x$ac_cv_header_doctest_doctest_h" != "xyes"], [
        AC_MSG_ERROR([system doctest not found])dnl
    ])
])
AS_IF([test "x$with_system_doctest" != "xyes"], [
    CPPFLAGS="-I\$(top_srcdir)/src/third-party/doctest-root $CPPFLAGS"
])


LNAV_WITH_JEMALLOC

LNAV_WITH_LOCAL_YAJL

AX_WITH_CURSES

AS_VAR_IF([ax_cv_curses],[yes],[],
    AC_MSG_ERROR([requires an X/Open-compatible Curses library with color])dnl
)

AX_PATH_LIB_ARCHIVE
AX_CHECK_PCRE2([8], [], [AC_MSG_ERROR([pcre2 is required to build])])
AX_PATH_LIB_READLINE

AX_CODE_COVERAGE

LNAV_WITH_SQLITE3("3.9.0")

AC_ARG_ENABLE(
    [system-paths],
    AS_HELP_STRING([--disable-system-paths],
                   [Add extra system paths]),,[enable_system_paths=yes])

if test x"${enable_system_paths}" != x"no"; then
    for defdir in /opt/local /usr/local; do
        if test -d "$defdir/include"; then
            echo "Adding include path: $defdir/include"
            CPPFLAGS="$CPPFLAGS -I$defdir/include"
        fi
    done

    for defdir in /opt/local /usr/local /usr /; do

        if test -d "$defdir/lib64"; then
            LDFLAGS="$LDFLAGS -L$defdir/lib64"
        fi
        if test -d "$defdir/lib"; then
            LDFLAGS="$LDFLAGS -L$defdir/lib"
        fi

        if test -d "$defdir/lib/x86_64-linux-gnu"; then
            LDFLAGS="$LDFLAGS -L$defdir/lib/x86_64-linux-gnu"
        fi
    done
else
    echo "Not including extra system paths"
fi

dnl case "$host_os" in
dnl     *)
dnl         # AC_DEFINE([_XOPEN_SOURCE], [500], [Need pread])
dnl         AC_DEFINE([_DEFAULT_SOURCE], [1], [Need pread])
dnl         AC_DEFINE([_BSD_SOURCE], [1], [Need pread])
dnl         ;;
dnl esac

AC_DEFINE([_XOPEN_SOURCE_EXTENDED], [1], [Wide character support for ncurses])

AS_VAR_SET(ALL_LDFLAGS, "$SQLITE3_LDFLAGS $READLINE_LDFLAGS $LIBARCHIVE_LDFLAGS $LIBCURL $LDFLAGS")

AS_VAR_SET(static_lib_list,
           ["libncurses.a libncursesw.a libreadline.a libsqlite3.a libz.a libtinfo.a libtinfow.a"])
AS_VAR_SET(static_lib_list,
           ["$static_lib_list libpcre2-8.a libncursesw.a libbz2.a"])
AS_VAR_SET(static_lib_list,
           ["$static_lib_list libgpm.a libcurl.a libcrypto.a libssl.a libssh2.a"])
AS_VAR_SET(static_lib_list,
           ["$static_lib_list libnghttp2.a liblzma.a libcrypto.a libzstd.a libldap.a"])
AS_VAR_SET(static_lib_list,
           ["$static_lib_list libarchive.a libidn2.a libgssapi_krb5.a libbrotlidec-static.a"])
AS_VAR_SET(static_lib_list,
           ["$static_lib_list librtmp.a libiconv.a liblz4.a liblber.a libunistring.a"])

if test x"${enable_static}" = x"yes"; then
    case "$host_os" in
    darwin*)
        STATIC_LDFLAGS="$STATIC_LDFLAGS -Wl,-search_paths_first"
        ;;
    esac

    AX_CHECK_LINK_FLAG([-static-libgcc], [STATIC_LDFLAGS="$STATIC_LDFLAGS -static-libgcc"])

    STATIC_LDFLAGS="$STATIC_LDFLAGS -L`pwd`/src/static-libs"
    AX_CHECK_LINK_FLAG([-static-libstdc++],
                       [STATIC_LDFLAGS="$STATIC_LDFLAGS -static-libstdc++"])
    # This is a hack to link against static libraries instead of shared
    # so that we can build a mostly statically link exe that can
    # be downloaded and used right away.  This is required for OS X and
    # will, hopefully, make a static binary that is compatible with
    # many different versions of Linux.
    AS_MKDIR_P(src/static-libs)
    rm -f src/static-libs/*.a
    for libflag in $ALL_LDFLAGS; do
        case $libflag in
        -Lstatic-libs)
        ;;
        -L*)
                libdir=`echo $libflag | sed -e 's/-L//'`
                for slib in $static_lib_list; do
                    if test -e "$libdir/$slib" -a ! -e "src/static-libs/$slib"; then
                        ln -sf "$libdir/$slib" src/static-libs/.
                    fi
                done
        ;;
        esac
    done

    for slib in $static_lib_list; do
        AC_MSG_CHECKING(for static library $slib)
        if test -e "src/static-libs/$slib"; then
            found_slib=`readlink src/static-libs/$slib`
            AC_MSG_RESULT($found_slib)
        else
            AC_MSG_RESULT(not found)
        fi
    done
fi
AC_SUBST(STATIC_LDFLAGS)

AS_CASE(["$host_os"],
    [darwin*],
    [LDFLAGS="$LDFLAGS -framework CoreFoundation"],
    [
    curses_lib=$(echo $CURSES_LIB | sed -e 's/-l//')
    AS_IF([test $? -eq 0],
        [
        AS_CASE(["$curses_lib"],
            [ncurses*],
            [AS_VAR_SET_IF(HAVE_GPM, [],
                [
                AC_MSG_NOTICE([Checking for libgpm dependency])
                AS_VAR_SET(saved_LDFLAGS, $LDFLAGS)
                AS_VAR_SET(LDFLAGS, "$STATIC_LDFLAGS $LDFLAGS")
                AS_VAR_SET(saved_LIBS, $LIBS)
                AC_CHECK_LIB($curses_lib, mousemask,
                    [
                    AS_VAR_SET(LDFLAGS, $saved_LDFLAGS)
                    AS_VAR_SET(LIBS, $saved_LIBS)
                    ],
                    AC_MSG_ERROR([libgpm development libraries are required to build]))dnl
                ])dnl
            ])
        ],
        [
        AC_MSG_WARN([Unable to test for dependepncy on gpm.])
        AC_MSG_WARN([If build fails during make consider installing libgpm development libraries.])
        ])dnl
    ]dnl
)

saved_location=$(pwd)
cd $srcdir
version=$(echo $(git describe --abbrev=7 --dirty --always --tags 2>/dev/null) | sed -e 's/^v//' 2>/dev/null)
cd $saved_location
AS_IF([test $? -eq 0],
    [version=$(echo $version | tr -d '\n')
     version=${version:-${PACKAGE_VERSION}}
     AC_DEFINE_UNQUOTED([VCS_PACKAGE_STRING], ["$PACKAGE_NAME $version"],
         [VCS package string])],
    AC_DEFINE_UNQUOTED([VCS_PACKAGE_STRING], ["$PACKAGE_STRING"], [VCS package string]))

AM_CONDITIONAL(HAVE_CARGO, test x"$CARGO_CMD" != x"")
AM_CONDITIONAL(USE_INCLUDED_YAJL, test $HAVE_LOCAL_YAJL -eq 0)
AM_CONDITIONAL(HAVE_LIBCURL, test x"$LIBCURL" != x"")
AM_CONDITIONAL([CROSS_COMPILING], [ test x"$cross_compiling" != x"no" ])
AM_CONDITIONAL(HAVE_CHECK_JSONSCHEMA, test x"$CHECK_JSONSCHEMA" != x"")

AS_VAR_SET(USER_CXXFLAGS, ["$CXXFLAGS"])
AC_SUBST(USER_CXXFLAGS)

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([TESTS_ENVIRONMENT])
AC_CONFIG_FILES([tools/Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([src/base/Makefile])
AC_CONFIG_FILES([src/formats/logfmt/Makefile])
AC_CONFIG_FILES([src/fmtlib/Makefile])
AC_CONFIG_FILES([src/pcrepp/Makefile])
AC_CONFIG_FILES([src/pugixml/Makefile])
AC_CONFIG_FILES([src/tailer/Makefile])
AC_CONFIG_FILES([src/yajl/Makefile])
AC_CONFIG_FILES([src/yajlpp/Makefile])
AC_CONFIG_FILES([src/third-party/base64/lib/Makefile])
AC_CONFIG_FILES([src/third-party/date/src/Makefile])
AC_CONFIG_FILES([src/third-party/scnlib/src/Makefile])
AC_CONFIG_FILES([test/Makefile])

AC_OUTPUT
