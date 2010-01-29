AC_DEFUN([AX_PATH_LIB_PCRE],[dnl
AC_MSG_CHECKING([lib pcre])
AC_ARG_WITH(pcre,
[  --with-pcre[[=prefix]]],,
     with_pcre="yes")
if test ".$with_pcre" = ".no" ; then
  AC_MSG_RESULT([disabled])
  m4_ifval($2,$2)
else
  AC_MSG_RESULT([(testing)])
  AC_CHECK_LIB(pcre, pcre_study)
  AC_CHECK_HEADERS(pcre.h pcre/pcre.h)
  if test "$ac_cv_lib_pcre_pcre_study" = "yes" ; then
     PCRE_LIBS="-lpcre"
     AC_MSG_CHECKING([lib pcre])
     AC_MSG_RESULT([$PCRE_LIBS])
     m4_ifval($1,$1)
  else
     OLDLDFLAGS="$LDFLAGS" ; LDFLAGS="$LDFLAGS -L$with_pcre/lib"
     OLDCPPFLAGS="$CPPFLAGS" ; CPPFLAGS="$CPPFLAGS -I$with_pcre/include"
     AC_CHECK_LIB(pcre, pcre_compile)
     CPPFLAGS="$OLDCPPFLAGS"
     LDFLAGS="$OLDLDFLAGS"
     if test "$ac_cv_lib_pcre_pcre_compile" = "yes" ; then
        AC_MSG_RESULT(.setting PCRE_LIBS -L$with_pcre/lib -lpcre)
        PCRE_LDFLAGS="-L$with_pcre/lib"
        PCRE_LIBS="-lpcre"
        test -d "$with_pcre/include" && PCRE_CFLAGS="-I$with_pcre/include"
        AC_MSG_CHECKING([lib pcre])
        AC_MSG_RESULT([$PCRE_LIBS])
        m4_ifval($1,$1)
     else
        AC_MSG_CHECKING([lib pcre])
        AC_MSG_RESULT([no, (WARNING)])
        m4_ifval($2,$2)
     fi
  fi
fi
AC_SUBST([PCRE_LIBS])
AC_SUBST([PCRE_CFLAGS])
])

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
    AC_CHECK_LIB(readline, readline, [], [], [-ltermcap])
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
])


AC_DEFUN([MP_WITH_CURSES],
  [AC_ARG_WITH(ncurses, [  --with-ncurses          Force the use of ncurses over curses],,)
   mp_save_LIBS="$LIBS"
   CURSES_LIB=""
   if test "$with_ncurses" != yes
   then
     AC_CACHE_CHECK([for working curses], mp_cv_curses,
       [LIBS="$LIBS -lcurses"
        AC_TRY_LINK(
          [#include <curses.h>],
          [chtype a; int b=A_STANDOUT, c=KEY_LEFT; initscr(); ],
          mp_cv_curses=yes, mp_cv_curses=no)])
     if test "$mp_cv_curses" = yes
     then
       AC_DEFINE(HAVE_CURSES_H, [], [curses library])
       CURSES_LIB="-lcurses"
     fi
   fi
   if test ! "$CURSES_LIB"
   then
     AC_CACHE_CHECK([for working ncurses], mp_cv_ncurses,
       [LIBS="$mp_save_LIBS -lncurses"
        AC_TRY_LINK(
          [#include <ncurses.h>],
          [chtype a; int b=A_STANDOUT, c=KEY_LEFT; initscr(); ],
          mp_cv_ncurses=yes, mp_cv_ncurses=no)])
     if test "$mp_cv_ncurses" = yes
     then
       AC_DEFINE(HAVE_NCURSES_H, [], [curses library])
       CURSES_LIB="-lncurses"
     fi
   fi
   LIBS="$mp_save_LIBS"
   AC_SUBST(CURSES_LIB)
])dnl
