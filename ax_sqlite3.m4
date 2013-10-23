dnl $Id: ax_sqlite3.m4,v 1.2 2006/08/30 14:28:55 mloskot Exp $
dnl
dnl @synopsis AX_LIB_SQLITE3([MINIMUM-VERSION])
dnl 
dnl Test for the SQLite 3 library of a particular version (or newer)
dnl
dnl This macro takes only one optional argument, required version
dnl of SQLite 3 library. If required version is not passed,
dnl 3.0.0 is used in the test of existance of SQLite 3.
dnl
dnl If no intallation prefix to the installed SQLite library is given
dnl the macro searches under /usr, /usr/local, and /opt.
dnl
dnl This macro calls:
dnl
dnl   AC_SUBST(SQLITE3_CFLAGS)
dnl   AC_SUBST(SQLITE3_LDFLAGS)
dnl   AC_SUBST(SQLITE3_LIBS)
dnl   AC_SUBST(SQLITE3_VERSION)
dnl
dnl And sets:
dnl
dnl   HAVE_SQLITE3
dnl
dnl @category InstalledPackages
dnl @category Cxx
dnl @author Mateusz Loskot <mateusz@loskot.net>
dnl @version $Date: 2006/08/30 14:28:55 $
dnl @license AllPermissive
dnl
dnl $Id: ax_sqlite3.m4,v 1.2 2006/08/30 14:28:55 mloskot Exp $
dnl
AC_DEFUN([AX_LIB_SQLITE3],
[
    AC_ARG_WITH([sqlite3],
        AC_HELP_STRING(
            [--with-sqlite3=@<:@ARG@:>@],
            [use SQLite 3 library @<:@default=yes@:>@, optionally specify the prefix for sqlite3 library]
        ),
        [
        if test "$withval" = "no"; then
            WANT_SQLITE3="no"
        elif test "$withval" = "yes"; then
            WANT_SQLITE3="yes"
            ac_sqlite3_path=""
        else
            WANT_SQLITE3="yes"
            ac_sqlite3_path="$withval"
        fi
        ],
        [WANT_SQLITE3="yes"]
    )

    SQLITE3_CFLAGS=""
    SQLITE3_LDFLAGS=""
    SQLITE3_LIBS=""
    SQLITE3_VERSION=""

    if test "x$WANT_SQLITE3" = "xyes"; then

        ac_sqlite3_header="sqlite3.h"
        
        sqlite3_version_req=ifelse([$1], [], [3.0.0], [$1])
        sqlite3_version_req_shorten=`expr $sqlite3_version_req : '\([[0-9]]*\.[[0-9]]*\)'`
        sqlite3_version_req_major=`expr $sqlite3_version_req : '\([[0-9]]*\)'`
        sqlite3_version_req_minor=`expr $sqlite3_version_req : '[[0-9]]*\.\([[0-9]]*\)'`
        sqlite3_version_req_micro=`expr $sqlite3_version_req : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
        if test "x$sqlite3_version_req_micro" = "x" ; then
            sqlite3_version_req_micro="0"
        fi

        sqlite3_version_req_number=`expr $sqlite3_version_req_major \* 1000000 \
                                   \+ $sqlite3_version_req_minor \* 1000 \
                                   \+ $sqlite3_version_req_micro`

        AC_MSG_CHECKING([for SQLite3 library >= $sqlite3_version_req])

        if test "$ac_sqlite3_path" != ""; then
            ac_sqlite3_ldflags="-L$ac_sqlite3_path/lib"
            ac_sqlite3_cppflags="-I$ac_sqlite3_path/include"
        else
            for ac_sqlite3_path_tmp in /usr /usr/local /opt ; do
                if test -f "$ac_sqlite3_path_tmp/include/$ac_sqlite3_header" \
                    && test -r "$ac_sqlite3_path_tmp/include/$ac_sqlite3_header"; then
                    ac_sqlite3_path=$ac_sqlite3_path_tmp
                    ac_sqlite3_cppflags="-I$ac_sqlite3_path_tmp/include"
                    ac_sqlite3_ldflags="-L$ac_sqlite3_path_tmp/lib"
                    break;
                fi
            done
        fi

        ac_sqlite3_ldflags="$ac_sqlite3_ldflags"
	ac_sqlite3_libs="-lsqlite3"

        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $ac_sqlite3_cppflags"

        AC_LANG_PUSH(C++)
        AC_COMPILE_IFELSE(
            [
            AC_LANG_PROGRAM([[@%:@include <sqlite3.h>]],
                [[
#if (SQLITE_VERSION_NUMBER >= $sqlite3_version_req_number)
// Everything is okay
#else
#  error SQLite version is too old
#endif
                ]]
            )
            ],
            [
            AC_MSG_RESULT([yes])
            success="yes"
            ],
            [
            AC_MSG_RESULT([not found])
            succees="no"
            ]
        )
        AC_LANG_POP([C++])

        CPPFLAGS="$saved_CPPFLAGS"

        if test "$success" = "yes"; then
            
            SQLITE3_CFLAGS="$ac_sqlite3_cppflags"
            SQLITE3_LDFLAGS="$ac_sqlite3_ldflags"
            SQLITE3_LIBS="$ac_sqlite3_libs"

            ac_sqlite3_header_path="$ac_sqlite3_path/include/$ac_sqlite3_header"

            dnl Retrieve SQLite release version
            if test "x$ac_sqlite3_header_path" != "x"; then
                ac_sqlite3_version=`cat $ac_sqlite3_header_path \
                    | grep '#define.*SQLITE_VERSION.*\"' | sed -e 's/.* "//' \
                        | sed -e 's/"//'`
                if test $ac_sqlite3_version != ""; then
                    SQLITE3_VERSION=$ac_sqlite3_version
                else
                    AC_MSG_WARN([Can not find SQLITE_VERSION macro in sqlite3.h header to retrieve SQLite version!])
                fi
            fi

            AC_DEFINE([HAVE_SQLITE3], [], [sqlite3])
        fi
    fi

    AC_SUBST(SQLITE3_CFLAGS)
    AC_SUBST(SQLITE3_LDFLAGS)
    AC_SUBST(SQLITE3_LIBS)
    AC_SUBST(SQLITE3_VERSION)
])

