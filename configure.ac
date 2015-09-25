AC_INIT([lnav],[0.8.0],[lnav@googlegroups.com],[lnav],[http://lnav.org])
AC_CONFIG_SRCDIR([src/lnav.cc])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

AC_PREFIX_DEFAULT(/usr)

AC_CANONICAL_HOST

for defdir in /opt/local /usr/local /usr /; do
    if test -d "$defdir/include"; then
        CPPFLAGS="$CPPFLAGS -I$defdir/include"
    fi

    if test -d "$defdir/lib"; then
        LDFLAGS="$LDFLAGS -L$defdir/lib"
    fi
    if test -d "$defdir/lib64"; then
        LDFLAGS="$LDFLAGS -L$defdir/lib64"
    fi

    if test -d "$defdir/lib/x86_64-linux-gnu"; then
        LDFLAGS="$LDFLAGS -L$defdir/lib/x86_64-linux-gnu"
    fi
done

dnl abssrcdir is the absolute path to the source base (regardless of where
dnl you are building it)
AS_CASE([x$srcdir],
    [x/*],
    AS_VAR_SET(abssrcdir, $srcdir),
    AS_VAR_SET(abssrcdir, `pwd`/$srcdir)
)

AC_SUBST(abssrcdir)

AC_PROG_CXX

CPPFLAGS="$CPPFLAGS -D_ISOC99_SOURCE -D__STDC_LIMIT_MACROS"

# CFLAGS=`echo $CFLAGS | sed 's/-O2//g'`
# CXXFLAGS=`echo $CXXFLAGS | sed 's/-O2//g'`

AC_ARG_VAR(SFTP_TEST_URL)

AC_ARG_ENABLE([profiling],
              AS_HELP_STRING([--enable-profiling],
                             [Compile with gprof(1) profiling support]))

AC_MSG_CHECKING(gprof(4) profiling support)

AS_VAR_IF([enable_profiling], [yes],
      [CFLAGS="$CFLAGS -pg -gstabs"
       CXXFLAGS="$CXXFLAGS -pg -gstabs"
       LDFLAGS="$LDFLAGS -pg"],
      [enable_profiling=no]dnl
)

AC_MSG_RESULT($enable_profiling)

AC_SUBST(CFLAGS_PG)

AC_PROG_INSTALL
AC_PROG_RANLIB
AC_PROG_LN_S
AC_PROG_MAKE_SET

AC_PATH_PROG(BZIP2_CMD, [bzip2])
AC_PATH_PROG(RE2C_CMD, [re2c])
AM_CONDITIONAL(HAVE_RE2C, test x"$RE2C_CMD" != x"")

AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(size_t)

AC_STRUCT_TIMEZONE

AC_ARG_ENABLE([static],
              AS_HELP_STRING([--disable-static],
                             [Disable static linking]))

AC_SEARCH_LIBS(openpty, util)
AC_SEARCH_LIBS(gzseek, z, [], [AC_MSG_ERROR([libz required to build])])
AC_SEARCH_LIBS(BZ2_bzopen, bz2,
     AS_VAR_SET(BZIP2_SUPPORT, 1),
     AS_VAR_SET(BZIP2_SUPPORT, 0))
AC_SUBST(BZIP2_SUPPORT)
AC_SEARCH_LIBS(dlopen, dl)
AC_SEARCH_LIBS(backtrace, execinfo)
LIBCURL_CHECK_CONFIG([], [7.23.0], [], [], [test x"${enable_static}" != x"no"])

# Sometimes, curses depends on these libraries being linked in...
AC_ARG_ENABLE([tinfo],
    AS_HELP_STRING([--disable-tinfo],
        [Disable linking with tinfo, enabled by default]),
    [], [enable_tinfo="yes"]
)

AS_IF([test "x${enable_tinfo}" != "xno"],
    [dnl
    AC_MSG_NOTICE([Trying to link with tinfo])
    AC_SEARCH_LIBS(cur_term, tinfo,
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
    AC_SEARCH_LIBS(Gpm_Open, gpm,
       AS_VAR_SET(HAVE_GPM, "1"),
       AC_MSG_WARN(m4_join([ ],
                   [libgpm not found. If build fails later],
                   [consider installing gpm dev package])dnl
       )
    )
)

AC_CHECK_HEADERS(execinfo.h pty.h util.h zlib.h bzlib.h libutil.h sys/ttydefaults.h)

LNAV_WITH_JEMALLOC

LNAV_WITH_LOCAL_YAJL

AX_WITH_CURSES

AS_VAR_IF([ax_cv_curses],[yes],[],
    AC_MSG_ERROR([requires an X/Open-compatible Curses library with color])dnl
)

AX_PATH_LIB_PCRE([], [AC_MSG_ERROR([pcre required to build])])
AX_PATH_LIB_READLINE

LNAV_WITH_SQLITE3("3.6.0")

case "$host_os" in
    *)
        # AC_DEFINE([_XOPEN_SOURCE], [500], [Need pread])
        AC_DEFINE([_BSD_SOURCE], [1], [Need pread])
        ;;
esac

AS_VAR_SET(ALL_LDFLAGS, "$SQLITE3_LDFLAGS $READLINE_LDFLAGS $LDFLAGS")

AS_VAR_SET(static_lib_list,
           ["libncurses.a libreadline.a libsqlite3.a libz.a libtinfo.a"])
AS_VAR_SET(static_lib_list,
           ["$static_lib_list libpcre.a libpcrecpp.a libncursesw.a libbz2.a"])
AS_VAR_SET(static_lib_list,
           ["$static_lib_list libgpm.a libcurl.a libcrypto.a libssl.a libssh2.a"])

if test x"${enable_static}" != x"no"; then
    case "$host_os" in
    darwin*)
        STATIC_LDFLAGS="$STATIC_LDFLAGS -Wl,-search_paths_first"
        ;;
    esac

    STATIC_LDFLAGS="$STATIC_LDFLAGS -L`pwd`/src/static-libs"
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
                    if test -e "$libdir/$slib"; then
                        ln -sf "$libdir/$slib" src/static-libs/.
                    fi
                done
        ;;
        esac
    done
fi
AC_SUBST(STATIC_LDFLAGS)

AS_CASE(["$host_os"],
    [darwin*],
    [],
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
version=$(expr $(git describe --abbrev=7 --dirty --always --tags 2>/dev/null) : 'v\([[0-9]]*\.[[0-9]]*\.[[0-9]]*.*\)' 2>/dev/null)
cd $saved_location
AS_IF([test $? -eq 0],
    [version=$(echo $version | tr -d '\n')
     version=${version:-${PACKAGE_VERSION}}
     AC_DEFINE_UNQUOTED([VCS_PACKAGE_STRING], ["$PACKAGE_NAME $version"],
         [VCS package string])],
    AC_DEFINE_UNQUOTED([VCS_PACKAGE_STRING], ["$PACKAGE_STRING"], [VCS package string]))

AM_CONDITIONAL(USE_INCLUDED_YAJL, test $HAVE_LOCAL_YAJL -eq 0)
AM_CONDITIONAL(HAVE_LIBCURL, test x"$LIBCURL" != x"")

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([TESTS_ENVIRONMENT])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([test/Makefile])

AC_OUTPUT
