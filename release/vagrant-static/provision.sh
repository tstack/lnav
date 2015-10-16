#! /usr/bin/env bash

FAKE_ROOT=/home/vagrant/fake.root

rm -rf ~/extract
mkdir -p ${FAKE_ROOT} ~/packages ~/extract ~/github

export PATH=${FAKE_ROOT}/bin:${PATH}

cd ~/github

PACKAGE_URLS="\
    http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz \
    http://ftp.gnu.org/gnu/automake/automake-1.15.tar.gz \
    ftp://invisible-island.net/ncurses/ncurses-5.9.tar.gz \
    ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-8.37.tar.gz \
    ftp://ftp.cwru.edu/pub/bash/readline-6.3.tar.gz \
    http://zlib.net/zlib-1.2.8.tar.gz \
    http://www.bzip.org/1.0.6/bzip2-1.0.6.tar.gz \
    http://sqlite.org/2015/sqlite-autoconf-3090000.tar.gz \
    http://openssl.org/source/openssl-1.0.1p.tar.gz \
    http://www.libssh2.org/download/libssh2-1.6.0.tar.gz \
    http://curl.haxx.se/download/curl-7.45.0.tar.gz \
    "

SQLITE_CFLAGS="\
    -DSQLITE_ENABLE_COLUMN_METADATA \
    -DSQLITE_SOUNDEX \
    -DSQLITE_ENABLE_DBSTAT_VTAB \
    -DSQLITE_ENABLE_API_ARMOR \
    -DSQLITE_ENABLE_JSON1 \
    "

cd ~/packages

for url in $PACKAGE_URLS; do
    wget --no-check-certificate -N $url
done

cd ~/extract

for pkg in ~/packages/*.tar.gz; do
    tar xfz $pkg
done

(cd autoconf-2.69 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd automake-1.15 && ./configure --prefix=${FAKE_ROOT} && make && make install)

OS=$(uname -s)


(cd readline-6.3 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd bzip2-1.0.6 && make install PREFIX=${FAKE_ROOT})

(cd sqlite-* &&
 ./configure --prefix=${FAKE_ROOT} \
     CFLAGS="${SQLITE_CFLAGS}" \
     && \
 make && make install)

(cd openssl-* &&
 ./config --prefix=${FAKE_ROOT} -fPIC &&
 make &&
 make install)

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

    (cd pcre-8.37 && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-jit \
         --enable-utf \
        CC="gcc44" \
        CXX="g++44" \
         && \
     make && make install)

    (cd zlib-1.2.8 && ./configure --prefix=${FAKE_ROOT} && make && make install)

    (cd libssh2-* &&
     ./configure --prefix=${FAKE_ROOT} \
         --with-libssl-prefix=/home/vagrant/fake.root \
         --with-libz-prefix=/home/vagrant/fake.root \
         "LDFLAGS=-ldl" &&
     make &&
     make install)

    (cd curl-* &&
     ./configure --prefix=${FAKE_ROOT} \
         --with-libssh2=${FAKE_ROOT} \
         --with-ssl=${FAKE_ROOT} \
         --with-zlib=${FAKE_ROOT} \
         "LDFLAGS=-ldl" &&
     make &&
     make install)
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

    (cd pcre-8.37 && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-jit \
         --enable-utf \
         && \
     make && make install)

    (cd zlib-1.2.8 && ./configure --prefix=${FAKE_ROOT} "CFLAGS=-fPIC" \
        && make && make install)

    (cd libssh2-* &&
     ./configure --prefix=${FAKE_ROOT} \
         --with-libssl-prefix=/home/vagrant/fake.root \
         --with-libz-prefix=/home/vagrant/fake.root \
         &&
     make &&
     make install)

    (cd curl-* &&
     ./configure --prefix=${FAKE_ROOT} \
         --with-libssh2=${FAKE_ROOT} \
         --with-ssl=${FAKE_ROOT} \
         --with-zlib=${FAKE_ROOT} \
         "CFLAGS=-fPIC" &&
     make &&
     make install)
fi
