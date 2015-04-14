#! /usr/bin/env bash

FAKE_ROOT=/home/vagrant/fake.root

rm -rf ~/extract
mkdir -p ~/fake.root ~/packages ~/extract ~/github

export PATH=${FAKE_ROOT}/bin:${PATH}

cd ~/github

PACKAGE_URLS="\
    http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz \
    http://ftp.gnu.org/gnu/automake/automake-1.15.tar.gz \
    ftp://invisible-island.net/ncurses/ncurses-5.9.tar.gz \
    ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-8.36.tar.gz \
    ftp://ftp.cwru.edu/pub/bash/readline-6.3.tar.gz \
    http://zlib.net/zlib-1.2.8.tar.gz \
    http://www.bzip.org/1.0.6/bzip2-1.0.6.tar.gz \
    http://sqlite.org/2015/sqlite-autoconf-3080803.tar.gz"

cd ~/packages

for url in $PACKAGE_URLS; do
    wget -N $url
done

cd ~/extract

for pkg in ~/packages/*.tar.gz; do
    tar xfz $pkg
done

(cd autoconf-2.69 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd automake-1.15 && ./configure --prefix=${FAKE_ROOT} && make && make install)

OS=$(uname -s)

if test x"${OS}" != x"FreeBSD"; then
    (cd ncurses-5.9 && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-ext-mouse \
         --enable-sigwinch \
         --with-default-terminfo-dir=/usr/share/terminfo \
         --enable-ext-colors \
         --enable-widec \
        CC="gcc44" \
        CXX="g++44" \
         && \
     make && make install)

    (cd pcre-8.36 && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-jit \
         --enable-utf \
        CC="gcc44" \
        CXX="g++44" \
         && \
     make && make install)
else
    (cd ncurses-5.9 && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-ext-mouse \
         --enable-sigwinch \
         --with-default-terminfo-dir=/usr/share/terminfo \
         --enable-ext-colors \
         --enable-widec \
         && \
     make && make install)

    (cd pcre-8.36 && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-jit \
         --enable-utf \
         && \
     make && make install)
fi

(cd readline-6.3 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd zlib-1.2.8 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd bzip2-1.0.6 && make install PREFIX=${FAKE_ROOT})

(cd sqlite-* &&
 ./configure --prefix=${FAKE_ROOT} \
     CFLAGS="-DSQLITE_ENABLE_COLUMN_METADATA -DSQLITE_SOUNDEX" \
     && \
 make && make install)
