dnl
dnl Copyright (c) 2007-2015, Timothy Stack
dnl Copyright (c) 2015, Suresh Sundriyal
dnl
dnl All rights reserved.
dnl
dnl Redistribution and use in source and binary forms, with or without
dnl modification, are permitted provided that the following conditions are met:
dnl
dnl dnl Redistributions of source code must retain the above copyright notice, this
dnl list of conditions and the following disclaimer.
dnl dnl Redistributions in binary form must reproduce the above copyright notice,
dnl this list of conditions and the following disclaimer in the documentation
dnl and/or other materials provided with the distribution.
dnl dnl Neither the name of Timothy Stack nor the names of its contributors
dnl may be used to endorse or promote products derived from this software
dnl without specific prior written permission.
dnl
dnl THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
dnl EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
dnl WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
dnl DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
dnl DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
dnl (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
dnl LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
dnl ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
dnl (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
dnl SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
dnl
dnl @file lnav_with_readline.m4
dnl
AC_DEFUN([AX_PATH_LIB_READLINE],
    [dnl
    AC_REQUIRE([AX_WITH_CURSES])
    AC_MSG_CHECKING([lib readline])
    AC_ARG_WITH([readline],
        AS_HELP_STRING([--with-readline@<:@=prefix@:>@],dnl
            [compile xmlreadline part (via libreadline check)]),
        [],
        [with_readline="yes"]
    )dnl

    AS_VAR_SET(saved_CFLAGS, $CFLAGS)
    AS_VAR_SET(saved_CPPFLAGS, $CPPFLAGS)
    AS_VAR_SET(saved_LDFLAGS, $LDFLAGS)
    AS_VAR_SET(saved_LIBS, $LIBS)
    AS_CASE(["$with_readline"],
        [no],
        AC_MSG_ERROR([readline required to build]),
        [yes],
        [],
        [dnl
        AS_VAR_SET([READLINE_CFLAGS], ["-I$with_readline/include"])
        AS_VAR_SET([READLINE_LDFLAGS], ["-L$with_readline/lib"])
        LNAV_ADDTO(CFLAGS, [-I$with_readline/include])
        LNAV_ADDTO(CPPFLAGS, [-I$with_readline/include])
        dnl We want the provided path to be the first in the search order.
        LDFLAGS="-L$with_readline/lib $LDFLAGS"
        ]dnl
    )

    AC_SEARCH_LIBS([readline], [readline],
        [AS_VAR_SET([READLINE_LIBS], ["-lreadline"])],
        [AC_MSG_ERROR([libreadline library not found])],
        [$CURSES_LIB]dnl
    )dnl

    dnl Ensure that the readline library has the required symbols.
    dnl i.e. We haven't picked up editline.
    AC_SEARCH_LIBS([history_set_history_state], [readline],
        [],
        AC_MSG_ERROR([libreadline does not have the required symbols. editline possibly masquerading as readline.]),
        [$CURSES_LIB]dnl
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
    AS_VAR_SET(CFLAGS, $saved_CFLAGS)
    AS_VAR_SET(CPPFLAGS, $saved_CPPFLAGS)
    AS_VAR_SET(LDFLAGS, $saved_LDFLAGS)
    AS_VAR_SET(LIBS, $saved_LIBS)

    AC_SUBST([READLINE_LIBS])
    AC_SUBST([READLINE_CFLAGS])
    AC_SUBST([READLINE_LDFLAGS])
    ]dnl
)dnl
