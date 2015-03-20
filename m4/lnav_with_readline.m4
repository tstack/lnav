AC_DEFUN([AX_PATH_LIB_READLINE],[dnl
AC_MSG_CHECKING([lib readline])
AC_ARG_WITH(readline,
[  --with-readline[[=prefix]]    compile xmlreadline part (via libreadline check)],,
     with_readline="yes")
if test ".$with_readline" = ".no" ; then
  AC_MSG_RESULT([disabled])
  m4_ifval($2,$2)
else
  if test ".$with_readline" = ".yes"; then
    OLD_LIBS="$LIBS"
    AC_CHECK_LIB(readline, readline, [], [], [$CURSES_LIB])
    LIBS="$OLD_LIBS"
    if test "$ac_cv_lib_readline_readline" = "yes"; then
      READLINE_LIBS="-lreadline"
      AC_MSG_CHECKING([lib readline])
      AC_MSG_RESULT([$READLINE_LIBS])
      m4_ifval($1,$1)
    else
      AC_MSG_CHECKING([lib readline])
      AC_MSG_RESULT([no, (WARNING)])
      m4_ifval($2,$2)
    fi
  else
    LIBS="$LIBS $with_readline/lib/libreadline.a"
    OLDCPPFLAGS="$CPPFLAGS" ; CPPFLAGS="$CPPFLAGS -I$with_readline/include"
    AC_CHECK_HEADERS(readline.h readline/readline.h)
    CPPFLAGS="$OLDCPPFLAGS"
    READLINE_LIBS="$with_readline/lib/libreadline.a"
    test -d "$with_readline/include" && READLINE_CFLAGS="-I$with_readline/include"
    AC_MSG_CHECKING([lib readline])
    AC_MSG_RESULT([$READLINE_LIBS])
    m4_ifval($1,$1)
  fi
fi
AC_SUBST([READLINE_LIBS])
AC_SUBST([READLINE_CFLAGS])
])dnl
