AC_DEFUN([AX_PATH_LIB_READLINE],
    [
    AC_MSG_CHECKING([lib readline])
    AC_ARG_WITH([readline],
        AC_HELP_STRING(
            [--with-readline@<:@=prefix@:>@],
            [compile xmlreadline part (via libreadline check)]
        ),
        [],
        [with_readline="yes"]
    )dnl

    AS_CASE(["$with_readline"],
        [no],
        AC_MSG_ERROR([readline required to build]),
        [yes],
        [dnl
        AC_SEARCH_LIBS([readline], [readline],
            [AS_VAR_SET([READLINE_LIBS], ["-lreadline"])],
            [AC_MSG_ERROR([libreadline library not found])],
            [$CURSES_LIB]
        )dnl
        ],
        [dnl
        AS_VAR_SET([READLINE_LIBS], ["$with_readline/lib/libreadline.a"])
        AC_CHECK_FILE("$READLINE_LIBS", [],
            AC_MSG_ERROR([readline library not found])
        )dnl
        AS_VAR_SET([READLINE_CFLAGS], ["-I$with_readline/include"])
        AS_VAR_SET([READLINE_SAVED_LDFLAGS], ["$LDFLAGS"])
        LNAV_ADDTO(CPPFLAGS, ["-I$with_readline/include"])
        dnl We want the provided path to be the first in the search order.
        LDFLAGS="-L$with_readline/lib $LDFLAGS"
        ]dnl
    )

    AC_CHECK_HEADERS([readline.h readline/readline.h],
        [dnl
        AS_VAR_SET([HAVE_READLINE_HEADERS], [1])
        break
        ]dnl
    )


    AS_VAR_SET_IF([HAVE_READLINE_HEADERS], [],
        [AC_MSG_ERROR([readline headers not found])]
    )

    dnl Ensure that the readline library has the required symbols.
    dnl i.e. We haven't picked up editline.
    AC_SEARCH_LIBS([history_set_history_state], [readline],
        [
        AS_VAR_SET_IF([READLINE_SAVED_LDFLAGS],
            AS_VAR_SET([LDFLAGS], ["$READLINE_SAVED_LDFLAGS"])
        )
        ],
        AC_MSG_ERROR([libreadline does not have the required symbols. editline possibly masquerading as readline.])
        [$CURSES]
    )

    AC_SUBST([READLINE_LIBS])
    AC_SUBST([READLINE_CFLAGS])
    ]dnl
)dnl
