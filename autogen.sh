#! /bin/sh


if test x"${AUTORECONF}" = x""; then
    autoreconf -V 1>/dev/null 2>/dev/null
    if test $? -eq 0; then
        AUTORECONF=autoreconf
    fi
fi

if test x"${AUTORECONF}" != x""; then
    ${AUTORECONF} -vfi -I m4
else
    AUTOCONF=${AUTOCONF:-autoconf}
    AUTOMAKE=${AUTOMAKE:-automake}
    AUTOHEADER=${AUTOHEADER:-autoheader}
    ACLOCAL=${ACLOCAL:-aclocal}

    ${AUTOCONF} --version
    ${AUTOMAKE} --version

    ${ACLOCAL} -I m4 -I .
    ${AUTOHEADER} -I .
    ${AUTOMAKE} --add-missing --copy --force-missing --foreign
    ${AUTOCONF}
fi
