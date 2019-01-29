#! /usr/bin/env bash

if test x"${OS}" != x"FreeBSD"; then
    source scl_source enable devtoolset-4
fi

FAKE_ROOT=/home/vagrant/fake.root

rm -rf ~/extract
mkdir -p ${FAKE_ROOT} ~/packages ~/extract ~/github

export PATH=${FAKE_ROOT}/bin:${PATH}

cd ~/github

SQLITE_CFLAGS="\
    -DSQLITE_ENABLE_COLUMN_METADATA \
    -DSQLITE_SOUNDEX \
    -DSQLITE_ENABLE_DBSTAT_VTAB \
    -DSQLITE_ENABLE_API_ARMOR \
    -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_ENABLE_UPDATE_DELETE_LIMIT \
    "


cd ~/extract

for pkg in /vagrant/pkgs/*.tar.gz; do
    tar xfz $pkg
done

(cd make-4.2.1 && ./configure --prefix=${FAKE_ROOT} && make && make install)

OS=$(uname -s)


(cd readline-6.3 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd bzip2-1.0.6 && make install PREFIX=${FAKE_ROOT})

(cd sqlite-* &&
 ./configure --disable-editline --prefix=${FAKE_ROOT} \
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
         && \
     make && make install)

    (cd pcre-* && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-jit \
         --enable-utf \
         && \
     make && make install)

    (cd zlib-1.2.11 && ./configure --prefix=${FAKE_ROOT} && make && make install)

    (cd libssh2-* &&
     ./configure --prefix=${FAKE_ROOT} \
         --with-libssl-prefix=/home/vagrant/fake.root \
         --with-libz-prefix=/home/vagrant/fake.root \
         "CPPFLAGS=-I${FAKE_ROOT}/include" \
         "LDFLAGS=-ldl -L${FAKE_ROOT}/lib" &&
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

    (cd pcre-* && \
     ./configure --prefix=${FAKE_ROOT} \
         --enable-jit \
         --enable-utf \
         && \
     make && make install)

    (cd zlib-1.2.11 && ./configure --prefix=${FAKE_ROOT} "CFLAGS=-fPIC" \
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
