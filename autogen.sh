#! /bin/sh


AUTORECONF=${AUTORECONF:-$(which autoreconf)}

if [ -n ${AUTORECONF} ]; then
    ${AUTORECONF} -vfi -I m4
else
    AUTOCONF=${AUTOCONF:-$(which autoconf)}
    AUTOMAKE=${AUTOMAKE:-$(which automake)}
    AUTOHEADER=${AUTOHEADER:-$(which autoheader)}
    ACLOCAL=${ACLOCAL:-$(which aclocal)}

    ${AUTOCONF} --version
    ${AUTOMAKE} --version

    ${ACLOCAL} -I m4 -I .
    ${AUTOHEADER} -I .
    ${AUTOMAKE} --add-missing --copy --force-missing --foreign
    ${AUTOCONF}
fi
