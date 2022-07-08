#!/usr/bin/env bash

OS=$(uname -s)
if test x"${OS}" != x"FreeBSD"; then
    source scl_source enable devtoolset-9
fi

if test x"${OS}" != x"FreeBSD"; then
    MAKE=make
else
    MAKE=gmake
fi

FAKE_ROOT=/home/vagrant/fake.root

SRC_VERSION=$1

mkdir -p ~/github

cd ~/github
if ! test -d lnav; then
    git clone https://github.com/tstack/lnav.git
fi

cd ~/github/lnav
git restore .
git pull --rebase

if test -n "$SRC_VERSION"; then
    git checkout "$SRC_VERSION"
fi

saved_PATH=${PATH}
export PATH=${FAKE_ROOT}/bin:${PATH}
saved_LD_LIBRARY_PATH=${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${FAKE_ROOT}/lib
if test ! -f "configure"; then
    ./autogen.sh
    rm -rf ~/github/lbuild
    mkdir -p ~/github/lbuild
fi
cd ~/github/lbuild

TARGET_FILE='/vagrant/lnav-linux.zip'
if test x"${OS}" != x"FreeBSD"; then
    if test x"$(lsb_release | awk '{print $3}')" == x"Alpine"; then
        TARGET_FILE='/vagrant/lnav-musl.zip'
        ../lnav/configure \
            --with-libarchive=${FAKE_ROOT} \
            CFLAGS='-static -g1 -gz=zlib -no-pie -O2' \
            CXXFLAGS='-static -g1 -gz=zlib -U__unused -no-pie -O2' \
            LDFLAGS="-L${FAKE_ROOT}/lib" \
            CPPFLAGS="-I${FAKE_ROOT}/include" \
            LIBS="-L${FAKE_ROOT}/lib -lexecinfo -lssh2 -llzma -lssl -lcrypto -lz" \
            --enable-static
            PATH="${FAKE_ROOT}/bin:${PATH}"
    else
        ../lnav/configure \
            --enable-static \
            --with-libarchive=${FAKE_ROOT} \
            LDFLAGS="-L${FAKE_ROOT}/lib" \
            CPPFLAGS="-I${FAKE_ROOT}/include -O2" \
            LIBS="-L${FAKE_ROOT}/lib -lssh2 -llzma -lssl -lcrypto -lz" \
            PATH="${FAKE_ROOT}/bin:${PATH}"
    fi
else
    ../lnav/configure \
        --enable-static \
        LDFLAGS="-L${FAKE_ROOT}/lib -static" \
        LIBS="-lm -lelf" \
        CPPFLAGS="-I${FAKE_ROOT}/include -O2" \
        PATH="${FAKE_ROOT}/bin:${PATH}"
fi

${MAKE} -j2 && cp src/lnav /vagrant/lnav

if test x"${OS}" != x"FreeBSD"; then
    mkdir instdir
    make install DESTDIR=$PWD/instdir
    (cd instdir/ && zip -r "${TARGET_FILE}" .)
fi

export PATH=${saved_PATH}
export LD_LIBRARY_PATH=${saved_LD_LIBRARY_PATH}
