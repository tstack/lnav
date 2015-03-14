#! /bin/sh

autoconf --version
automake --version

aclocal -I .
autoheader -I .
automake --add-missing --copy --foreign
autoconf
