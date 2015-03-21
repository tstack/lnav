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
        LNAV_ADDTO(CPPFLAGS, ["-I$with_readline/include"])
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

    AC_SUBST([READLINE_LIBS])
    AC_SUBST([READLINE_CFLAGS])
    ]dnl
)dnl
