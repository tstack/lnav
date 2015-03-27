dnl
dnl @Modified from: ax_sqlite3.m4
dnl @author Mateusz Loskot <mateusz@loskot.net>
dnl @version $Date: 2006/08/30 14:28:55 $
dnl @license AllPermissive
dnl
dnl Copyright (c) 2015, Suresh Sundriyal
dnl
dnl @file lnav_with_sqlite3.m4
dnl
AC_DEFUN([LNAV_WITH_SQLITE3],
[dnl
    AC_ARG_WITH([sqlite3],
        AC_HELP_STRING(
            [--with-sqlite3=@<:@prefix@:>@],
            [compile with sqlite3]
        ),
        [],
        [with_sqlite3="yes"]
    )

    AS_CASE(["$with_sqlite3"],
        [no],
        AC_MSG_ERROR([sqlite3 required to build]),
        [yes],
        [],
        [dnl
        LNAV_ADDTO(CPPFLAGS, [-I$with_sqlite3/include])
        AS_VAR_SET([SQLITE3_LDFLAGS], ["-L$with_sqlite3/lib"])
        AS_VAR_SET([SQLITE3_CFLAGS], ["-I$with_sqlite3/include"])
        LDFLAGS="-L$with_sqlite3/lib $LDFLAGS"
        ]
    )

    AS_VAR_SET([saved_LDFLAGS], [$LDFLAGS])
    LNAV_ADDTO([LDFLAGS], [-pthread])
    AC_SEARCH_LIBS([sqlite3_open], [sqlite3],
        AS_VAR_SET([SQLITE3_LIBS], ["-lsqlite3"]),
        AC_MSG_ERROR([sqlite3 library not found])
    )
    AS_VAR_SET([LDFLAGS], [$saved_LDFLAGS])

    AC_CHECK_HEADERS([sqlite3.h], [],
        AC_MSG_ERROR([sqlite3 headers not found])
    )

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
        ],
        [
        AC_MSG_RESULT([not found])
        AC_MSG_ERROR([SQLite3 version >= $sqlite3_version_req is required])
        ]
    )
    AC_LANG_POP([C++])

    AC_CHECK_FUNC(sqlite3_stmt_readonly,
        AC_DEFINE([HAVE_SQLITE3_STMT_READONLY], [],
            [Have the sqlite3_stmt_readonly function]
        )
    )

    AC_SUBST(SQLITE3_CFLAGS)
    AC_SUBST(SQLITE3_LDFLAGS)
    AC_SUBST(SQLITE3_LIBS)
]
)
