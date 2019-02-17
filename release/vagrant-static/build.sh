#! /usr/bin/env bash

OS=$(uname -s)
if test x"${OS}" != x"FreeBSD"; then
    source scl_source enable devtoolset-4
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

if test x"${OS}" != x"FreeBSD"; then
    ../lnav/configure \
        LDFLAGS="-L${FAKE_ROOT}/lib" \
        CPPFLAGS="-I${FAKE_ROOT}/include" \
        PATH="${FAKE_ROOT}/bin:${PATH}"
else
    ../lnav/configure \
        LDFLAGS="-L${FAKE_ROOT}/lib -static" \
        LIBS="-lm -lelf" \
        CPPFLAGS="-I${FAKE_ROOT}/include" \
        PATH="${FAKE_ROOT}/bin:${PATH}"
fi

${MAKE} -j2 && strip -o /vagrant/lnav src/lnav

if test x"${OS}" != x"FreeBSD"; then
    mkdir instdir
    make install-strip DESTDIR=$PWD/instdir
    (cd instdir/ && zip -r /vagrant/lnav-linux.zip .)
fi

export PATH=${saved_PATH}
export LD_LIBRARY_PATH=${saved_LD_LIBRARY_PATH}
