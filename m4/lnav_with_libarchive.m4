dnl
dnl Copyright (c) 2020, Timothy Stack
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
dnl @file lnav_with_libarchive.m4
dnl
AC_DEFUN([AX_PATH_LIB_ARCHIVE],[dnl
AC_MSG_CHECKING([lib archive])
AC_ARG_WITH(libarchive,
[  --with-libarchive[[=prefix]]],,
     with_libarchive="yes")
if test ".$with_libarchive" = ".no" ; then
  AC_MSG_RESULT([disabled])
  m4_ifval($2,$2)
else
  AC_MSG_RESULT([(testing)])
  AC_CHECK_LIB(archive, archive_read_new)
  AC_CHECK_HEADERS(archive.h)
  if test "$ac_cv_lib_archive_archive_read_new" = "yes" && \
     test "x$ac_cv_header_archive_h" = xyes; then
     LIBARCHIVE_LIBS="-larchive"
     AC_MSG_CHECKING([lib archive])
     AC_MSG_RESULT([$LIBARCHIVE_LIBS])
     m4_ifval($1,$1)
  else
     unset ac_cv_header_archive_h
     OLDLDFLAGS="$LDFLAGS" ; LDFLAGS="$LDFLAGS -L$with_libarchive/lib"
     OLDCPPFLAGS="$CPPFLAGS" ; CPPFLAGS="$CPPFLAGS -I$with_libarchive/include"
     AC_CHECK_LIB(archive, archive_read_new)
     AC_CHECK_HEADERS(archive.h)
     CPPFLAGS="$OLDCPPFLAGS"
     LDFLAGS="$OLDLDFLAGS"
     if test "$ac_cv_lib_archive_archive_read_new" = "yes" && \
        test "x$ac_cv_header_archive_h" = xyes; then
        AC_MSG_RESULT(.setting LIBARCHIVE_LIBS -L$with_libarchive/lib -larchive)
        LIBARCHIVE_LDFLAGS="-L$with_libarchive/lib"
        LIBARCHIVE_LIBS="-larchive"
        test -d "$with_libarchive/include" && LIBARCHIVE_CFLAGS="-I$with_libarchive/include"
        AC_MSG_CHECKING([lib archive])
        AC_MSG_RESULT([$LIBARCHIVE_LIBS])
        m4_ifval($1,$1)
     else
        AC_MSG_CHECKING([lib archive])
        AC_MSG_RESULT([[no]])
        m4_ifval($2,$2)
     fi
  fi
fi
AC_SUBST([LIBARCHIVE_LIBS])
AC_SUBST([LIBARCHIVE_LDFLAGS])
AC_SUBST([LIBARCHIVE_CFLAGS])
])

