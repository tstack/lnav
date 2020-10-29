#! /usr/bin/env bash

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
    git clone git://github.com/tstack/lnav.git
fi

cd ~/github/lnav
git pull

if test -n "$SRC_VERSION"; then
    git checkout $SRC_VERSION
fi

saved_PATH=${PATH}
export PATH=${FAKE_ROOT}/bin:${PATH}
saved_LD_LIBRARY_PATH=${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${FAKE_ROOT}/lib
./autogen.sh

rm -rf ~/github/lbuild
mkdir -p ~/github/lbuild

cd ~/github/lbuild

TARGET_FILE='/vagrant/lnav-linux.zip'
if test x"${OS}" != x"FreeBSD"; then
    if test x"$(lsb_release | awk '{print $3}')" == x"Alpine"; then
        TARGET_FILE='/vagrant/lnav-musl.zip'
        ../lnav/configure \
            --with-libarchive=${FAKE_ROOT} \
            CFLAGS='-static -no-pie -s -O2' \
            CXXFLAGS='-static -U__unused -no-pie -s -O2' \
            LDFLAGS="-L${FAKE_ROOT}/lib" \
            CPPFLAGS="-I${FAKE_ROOT}/include" \
            --enable-static
            PATH="${FAKE_ROOT}/bin:${PATH}"
    else
        ../lnav/configure \
            --with-libarchive=${FAKE_ROOT} \
            LDFLAGS="-L${FAKE_ROOT}/lib" \
            CPPFLAGS="-I${FAKE_ROOT}/include -O2" \
            PATH="${FAKE_ROOT}/bin:${PATH}"
    fi
else
    ../lnav/configure \
        LDFLAGS="-L${FAKE_ROOT}/lib -static" \
        LIBS="-lm -lelf" \
        CPPFLAGS="-I${FAKE_ROOT}/include -O2" \
        PATH="${FAKE_ROOT}/bin:${PATH}"
fi

${MAKE} -j2 && strip -o /vagrant/lnav src/lnav

if test x"${OS}" != x"FreeBSD"; then
    mkdir instdir
    make install-strip DESTDIR=$PWD/instdir
    (cd instdir/ && zip -r "${TARGET_FILE}" .)
fi

export PATH=${saved_PATH}
export LD_LIBRARY_PATH=${saved_LD_LIBRARY_PATH}
