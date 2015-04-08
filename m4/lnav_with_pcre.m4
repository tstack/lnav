dnl
dnl Copyright (c) 2007-2015, Timothy Stack
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
dnl @file lnav_with_pcre.m4
dnl
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
  AS_VAR_SET(saved_LIBS, $LIBS)
  AS_VAR_SET(LIBS, "-lpcrecpp $LIBS")
  AC_MSG_CHECKING([libpcrecpp])
  AC_LANG_PUSH([C++])
  AC_LINK_IFELSE(
    [
    AC_LANG_PROGRAM([[@%:@include <pcrecpp.h>]],
        [[
        pcrecpp::RE("hello");
        ]]
    )
    ],
    AC_MSG_RESULT([yes]),
    [
    AC_MSG_RESULT([[no, (WARNING)]])
    AS_VAR_SET(LIBS, $saved_LIBS)
    ]
  )
  AC_LANG_POP([C++])
  AC_CHECK_HEADERS(pcre.h pcre/pcre.h)
  if test "$ac_cv_lib_pcre_pcre_study" = "yes" ; then
     PCRE_LIBS="-lpcre -lpcrecpp"
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
        PCRE_LIBS="-lpcre -lpcrecpp"
        test -d "$with_pcre/include" && PCRE_CFLAGS="-I$with_pcre/include"
        AC_MSG_CHECKING([lib pcre])
        AC_MSG_RESULT([$PCRE_LIBS])
        m4_ifval($1,$1)
     else
        AC_MSG_CHECKING([lib pcre])
        AC_MSG_RESULT([[no, (WARNING)]])
        m4_ifval($2,$2)
     fi
  fi
fi
AC_SUBST([PCRE_LIBS])
AC_SUBST([PCRE_CFLAGS])
])

