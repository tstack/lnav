# ===========================================================================
#      https://www.gnu.org/software/autoconf-archive/ax_check_pcre2.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_CHECK_PCRE2([bits], [action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   Search for an installed libpcre2-8 library. If nothing was specified
#   when calling configure, it searches first in /usr/local and then in
#   /usr, /opt/local and /sw. If the --with-pcre2=DIR is specified, it will
#   try to find it in DIR/include/pcre2.h and DIR/lib/libpcre2-8. If
#   --without-pcre2 is specified, the library is not searched at all.
#
#   If 'bits' is empty or '8', PCRE2 8-bit character support is checked
#   only. If 'bits' contains '16', PCRE2 8-bit and 16-bit character support
#   are checked. If 'bits' contains '32', PCRE2 8-bit and 32-bit character
#   support are checked. When 'bits' contains both '16' and '32', PCRE2
#   8-bit, 16-bit, and 32-bit character support is checked.
#
#   If either the header file (pcre2.h), or the library (libpcre2-8) is not
#   found, or the specified PCRE2 character bit width is not supported,
#   shell commands 'action-if-not-found' is run. If 'action-if-not-found' is
#   not specified, the configuration exits on error, asking for a valid
#   PCRE2 installation directory or --without-pcre2.
#
#   If both header file and library are found, and the specified PCRE2 bit
#   widths are supported, shell commands 'action-if-found' is run. If
#   'action-if-found' is not specified, the default action appends
#   '-I${PCRE2_HOME}/include' to CPFLAGS, appends '-L$PCRE2_HOME}/lib' to
#   LDFLAGS, prepends '-lpcre2-8' to LIBS, and calls AC_DEFINE(HAVE_PCRE2).
#   You should use autoheader to include a definition for this symbol in a
#   config.h file. Sample usage in a C/C++ source is as follows:
#
#     #ifdef HAVE_PCRE2
#     #define PCRE2_CODE_UNIT_WIDTH 8
#     #include <pcre2.h>
#     #endif /* HAVE_PCRE2 */
#
# LICENSE
#
#   Copyright (c) 2020 Robert van Engelen <engelen@acm.org>
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation; either version 2 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <https://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 2

AC_DEFUN([AX_CHECK_PCRE2],
#
# Handle user hints
#
[AC_MSG_CHECKING(if PCRE2 is wanted)
pcre2_places="/usr/local /usr /opt/local /sw"
AC_ARG_WITH([pcre2],
[  --with-pcre2=DIR        root directory path of PCRE2 installation @<:@defaults to
                          /usr/local or /usr if not found in /usr/local@:>@
  --without-pcre2         to disable PCRE2 usage completely],
[if test "$withval" != "no" ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    pcre2_places="$withval $pcre2_places"
  else
    AC_MSG_WARN([Sorry, $withval does not exist, checking usual places])
  fi
else
  pcre2_places=""
  AC_MSG_RESULT(no)
fi],
[AC_MSG_RESULT(yes)])
#
# Locate PCRE2, if wanted
#
if test -n "${pcre2_places}"
then
  # check the user supplied or any other more or less 'standard' place:
  #   Most UNIX systems      : /usr/local and /usr
  #   MacPorts / Fink on OSX : /opt/local respectively /sw
  for PCRE2_HOME in ${pcre2_places} ; do
    if test -f "${PCRE2_HOME}/include/pcre2.h"; then break; fi
    PCRE2_HOME=""
  done

  PCRE2_OLD_LDFLAGS=$LDFLAGS
  PCRE2_OLD_CPPFLAGS=$CPPFLAGS
  if test -n "${PCRE2_HOME}"; then
    LDFLAGS="$LDFLAGS -L${PCRE2_HOME}/lib"
    CPPFLAGS="$CPPFLAGS -I${PCRE2_HOME}/include"
  fi
  AC_LANG_PUSH([C])
  AC_CHECK_LIB([pcre2-8], [pcre2_compile_8], [pcre2_cv_libpcre2=yes], [pcre2_cv_libpcre2=no])
  AC_CHECK_HEADER([pcre2.h], [pcre2_cv_pcre2_h=yes], [pcre2_cv_pcre2_h=no], [#define PCRE2_CODE_UNIT_WIDTH 8])
  case "$1" in
  *16*)
    AC_CHECK_LIB([pcre2-16], [pcre2_compile_16], [pcre2_cv_libpcre2_16=yes], [pcre2_cv_libpcre2_16=no])
    AC_CHECK_HEADER([pcre2.h], [pcre2_cv_pcre2_16_h=yes], [pcre2_cv_pcre2_16_h=no], [#define PCRE2_CODE_UNIT_WIDTH 16])
    if test "$pcre2_cv_libpcre2_16" = "no" || test "$pcre2_cv_pcre2_16_h" = "no"; then
      pcre2_cv_libpcre2=no
    fi
    ;;
  esac
  case "$1" in
  *32*)
    AC_CHECK_LIB([pcre2-32], [pcre2_compile_32], [pcre2_cv_libpcre2_32=yes], [pcre2_cv_libpcre2_32=no])
    AC_CHECK_HEADER([pcre2.h], [pcre2_cv_pcre2_32_h=yes], [pcre2_cv_pcre2_32_h=no], [#define PCRE2_CODE_UNIT_WIDTH 32])
    if test "$pcre2_cv_libpcre2_32" = "no" || test "$pcre2_cv_pcre2_32_h" = "no"; then
      pcre2_cv_libpcre2=no
    fi
  esac
  AC_LANG_POP([C])
  if test "$pcre2_cv_libpcre2" = "yes" && test "$pcre2_cv_pcre2_h" = "yes"
  then
    #
    # If both library and header were found, action-if-found
    #
    m4_ifblank([$2],[
                CPPFLAGS="$CPPFLAGS -I${PCRE2_HOME}/include"
                LDFLAGS="$LDFLAGS -L${PCRE2_HOME}/lib"
                LIBS="-lpcre2-8 $LIBS"
                AC_DEFINE([HAVE_PCRE2], [1],
                          [Define to 1 if you have `PCRE2' library (-lpcre2-$1)])
               ],[
                # Restore variables
                LDFLAGS="$PCRE2_OLD_LDFLAGS"
                CPPFLAGS="$PCRE2_OLD_CPPFLAGS"
                $2
               ])
  else
    #
    # If either header or library was not found, action-if-not-found
    #
    m4_default([$3],[
                AC_MSG_ERROR([either specify a valid PCRE2 installation with --with-pcre2=DIR or disable PCRE2 usage with --without-pcre2])
                ])
  fi
fi
])
