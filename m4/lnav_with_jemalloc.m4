dnl -------------------------------------------------------- -*- autoconf -*-
dnl Licensed to the Apache Software Foundation (ASF) under one or more
dnl contributor license agreements.  See the NOTICE file distributed with
dnl this work for additional information regarding copyright ownership.
dnl The ASF licenses this file to You under the Apache License, Version 2.0
dnl (the "License"); you may not use this file except in compliance with
dnl the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl Unless required by applicable law or agreed to in writing, software
dnl distributed under the License is distributed on an "AS IS" BASIS,
dnl WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
dnl See the License for the specific language governing permissions and
dnl limitations under the License.

dnl
dnl lnav_with_jemalloc.m4: lnav's jemalloc autoconf macros
dnl
dnl
AC_DEFUN([LNAV_WITH_JEMALLOC], [
enable_jemalloc=no
AC_ARG_WITH([jemalloc], [AC_HELP_STRING([--with-jemalloc=DIR], [use a specific jemalloc library])],
[
    if test "$withval" != "no"; then
        enable_jemalloc=yes
        modify_env_variables=no
        case "$withval" in
            yes)
                AC_MSG_NOTICE([Checking for jemalloc libs])
                ;;
            *)
                jemalloc_include="$withval/include"
                jemalloc_ldflags="$withval/lib"
                modify_env_variables="yes"
                AC_MSG_NOTICE([Checking for jemalloc libs in $withval])
                ;;
        esac
    fi
])

jemalloch=0
if test "$enable_jemalloc" != "no"; then
    saved_ldflags=$LDFLAGS
    saved_cppflags=$CPPFLAGS
    saved_libtool_link_flags=$LIBTOOL_LINK_FLAGS
    jemalloc_have_headers=0
    jemalloc_have_libs=0

    if test "$modify_env_variables" != "no"; then
        LNAV_ADDTO(CPPFLAGS, [-I${jemalloc_include}])
        LNAV_ADDTO(LDFLAGS, [-L${jemalloc_ldflags}])
        LNAV_ADDTO(LIBTOOL_LINK_FLAGS, [-R${jemalloc_ldflags}])
    fi
    # On Darwin, jemalloc symbols are prefixed with je_. Search for that first,
    # then fall back to unadorned symbols.
    AC_SEARCH_LIBS([je_malloc_stats_print], [jemalloc], [jemalloc_have_libs=1],
        [AC_SEARCH_LIBS([malloc_stats_print], [jemalloc], [jemalloc_have_libs=1])]
    )
    if test "$jemalloc_have_libs" != "0"; then
        AC_MSG_NOTICE([Checking for jemalloc includes])
        AC_CHECK_HEADERS(jemalloc/jemalloc.h, [jemalloc_have_headers=1])
        if test "$jemalloc_have_headers" != "0"; then
            jemalloch=1
            LNAV_ADDTO(LIBS, [-ljemalloc])
        else
            AC_MSG_WARN([jemalloc headers not found])
        fi
    else
        AC_MSG_WARN([jemalloc libs not found])
    fi
    if test "$jemalloc_have_libs" = "0"; then
        CPPFLAGS=$saved_cppflags
        LDFLAGS=$saved_ldflags
        LIBTOOL_LINK_FLAGS=$saved_libtool_link_flags
        AC_MSG_WARN([jemalloc not found])
    fi
fi
AC_SUBST(jemalloch)
])dnl
