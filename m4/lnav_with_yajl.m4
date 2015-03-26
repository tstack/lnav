dnl
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
dnl @file lnav_with_yajl.m4
AC_DEFUN([LNAV_WITH_LOCAL_YAJL],
    [
    AC_ARG_WITH([yajl],
        AC_HELP_STRING(
            [--with-yajl=DIR],
            [use a local installed version of yajl]
        ),
        [
        AS_IF([test "$withval" != "no"],
            [
            AS_CASE(["$withval"],
                [yes],
                [AC_MSG_NOTICE([Checking for yajl libs])],
                [
                AS_VAR_SET([yajl_saved_ldflags], ["$LDFLAGS"])
                AS_VAR_SET([yajl_saved_cppflags], ["$CPPFLAGS"])
                AS_VAR_SET([yajl_saved_libtool_link_flags],
                    ["$LIBTOOL_LIBK_FLAGS"]
                )

                LNAV_ADDTO(CPPFLAGS, [-I$withval/include])
                LNAV_ADDTO(LDFLAGS, [-I$withval/lib])
                LNAV_ADDTO(LIBTOOL_LINK_FLAGS, [-R$withval/lib])

                AC_MSG_NOTICE([Checking for yajl libs in $withval])
                ]
            )

            AC_SEARCH_LIBS([yajl_gen_alloc],
                [yajl],
                [
                AC_MSG_NOTICE([Checking for yajl headers])
                AC_CHECK_HEADERS([yajl/yajl_parse.h],
                    AS_VAR_SET([HAVE_LOCAL_YAJL], [1]),
                    AC_MSG_WARN([yajl not found on the local system])
                )
                ]
            )
            ]
        )
        ]
    )

    AS_VAR_SET_IF([HAVE_LOCAL_YAJL],
        [],
        [
        AC_MSG_NOTICE([compiling with the included version of yajl])
        AS_VAR_SET([HAVE_LOCAL_YAJL], [0])
        AS_VAR_SET_IF([yajl_saved_ldflags],
            [
            AS_VAR_SET([CPPFLAGS], ["$yajl_saved_cppflags"])
            AS_VAR_SET([LDFLAGS], ["$yajl_saved_ldflags"])
            AS_VAR_SET([LIBTOOL_LIBK_FLAGS], ["$yajl_saved_libtool_link_flags"])
            ]
        )
        ]
    )

    AC_SUBST(HAVE_LOCAL_YAJL)
    ]
)dnl
