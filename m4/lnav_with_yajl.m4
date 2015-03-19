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

                LNAV_ADDTO(CPPFLAGS, ["-I$withval/include"])
                LNAV_ADDTO(LDFLAGS, ["-I$withval/lib"])
                LNAV_ADDTO(LIBTOOL_LINK_FLAGS, ["-R$withval/lib"])

                AC_MSG_NOTICE([Checking for yajl libs in $withval])
                ]
            )

            AC_SEARCH_LIBS([yajl_gen_alloc],
                [yajl],
                [
                AC_MSG_NOTICE([Checking for yajl headers])
                AC_CHECK_HEADERS([yajl/yajl_parse.h],
                    [
                    AS_VAR_SET([HAVE_LOCAL_YAJL], [1])
                    LNAV_ADDTO(LIBS, [-lyajl])
                    ],
                    [AC_MSG_WARN([yajl not found on the local system])]
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
