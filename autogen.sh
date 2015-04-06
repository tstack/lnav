#! /bin/sh


if [ -n ${AUTORECONF} ]; then
    autoreconf 1>/dev/null 2>/dev/null
    if [ $? ]; then
        AUTORECONF=autoreconf
    fi
fi

if [ -n ${AUTORECONF} ]; then
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
