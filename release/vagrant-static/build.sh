#! /usr/bin/env bash

FAKE_ROOT=/home/vagrant/fake.root

mkdir -p ~/github

cd ~/github
if ! test -d lnav; then
    git clone git://github.com/tstack/lnav.git
fi

cd ~/github/lnav
git pull
saved_PATH=${PATH}
saved_LD_LIBRARAY_PATH=${LD_LIBRARY_PATH}
export PATH=${PATH}:${FAKE_ROOT}/bin
export LD_LIBRARAY_PATH=${LD_LIBRARAY_PATH}:${FAKE_ROOT}/lib
./autogen.sh
export PATH=${saved_PATH}
export LD_LIBRARAY_PATH=${saved_LD_LIBRARY_PATH}

rm -rf ~/github/lbuild
mkdir -p ~/github/lbuild

cd ~/github/lbuild

OS=$(uname -s)
if test x"${OS}" != x"FreeBSD"; then
    ../lnav/configure \
        LDFLAGS="-L${FAKE_ROOT}/lib" \
        CC="gcc44" \
        CXX="g++44" \
        PATH="${FAKE_ROOT}/bin:${PATH}" \
        CPPFLAGS="-I${FAKE_ROOT}/include"
else
    ../lnav/configure \
        LDFLAGS="-L${FAKE_ROOT}/lib" \
        PATH="${FAKE_ROOT}/bin:${PATH}" \
        CPPFLAGS="-I${FAKE_ROOT}/include"
fi


make -j2 && strip -o /vagrant/lnav src/lnav
