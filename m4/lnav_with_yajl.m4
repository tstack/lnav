AC_DEFUN([LNAV_WITH_LOCAL_YAJL],
[
ENABLE_LOCAL_YAJL="no"
 AC_ARG_WITH([yajl],
    AC_HELP_STRING(
        [--with-yajl=DIR],
        [use a local installed version of yajl]
    ),
    [
    if test "$withval" != "no"; then
        ENABLE_LOCAL_YAJL="yes"
        modify_env_variables="no"
        case "$withval" in
            yes)
                AC_MSG_NOTICE([Checking for yajl libs])
                ;;
            *)
                yajl_include="$withval/include"
                yajl_ldflags="$withval/lib"
                modify_env_variables="yes"
                AC_MSG_NOTICE([Checking for yajl libs in $withval])
                ;;
        esac
    fi
    ]
)

HAVE_LOCAL_YAJL=0
YAJL_HAVE_LOCAL_HEADERS=0
YAJL_HAVE_LOCAL_LIBS=0
if test "$ENABLE_LOCAL_YAJL" != "no"; then
    saved_ldflags=$LDFLAGS
    saved_cppflags=$CPPFLAGS
    saved_libtool_link_flags=$LIBTOOL_LIBK_FLAGS

    if test "$modify_env_variables" != "no"; then
        LNAV_ADDTO(CPPFLAGS, [-I${yajl_include}])
        LNAV_ADDTO(LDFLAGS, [-L${yajl_ldflags}])
        LNAV_ADDTO(LIBTOOL_LINK_FLAGS, [-R${yajl_ldflags}])
    fi

    AC_SEARCH_LIBS([yajl_gen_alloc], [yajl], [YAJL_HAVE_LOCAL_LIBS=1])

    if test "$YAJL_HAVE_LOCAL_LIBS" != "0"; then
        AC_MSG_NOTICE([Checking for yajl headers])
        AC_CHECK_HEADERS([yajl/yajl_parse.h], [YAJL_HAVE_LOCAL_HEADERS=1])

        if test "$YAJL_HAVE_LOCAL_HEADERS" != "0"; then
            HAVE_LOCAL_YAJL=1
            LNAV_ADDTO(LIBS, [-lyajl])
        else
            AC_MSG_WARN([yajl not found on the local system])
        fi

        if test "$HAVE_LOCAL_YAJL" = "0"; then
            CPPFLAGS=$saved_cppflags
            LDFLAGS=$saved_ldflags
            LIBTOOL_LIBK_FLAGS=$saved_libtool_link_flags
        fi
    fi
fi

if test "$HAVE_LOCAL_YAJL" = "0"; then
    AC_MSG_NOTICE([compiling with the included version of yajl])
fi

AC_SUBST(HAVE_LOCAL_YAJL)

])dnl
