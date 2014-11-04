#! /bin/bash

FAKE_ROOT=/home/vagrant/fake.root

cd ~/github
if ! test -d lnav; then
    git clone git://github.com/tstack/lnav.git
fi

cd ~/github/lnav
git pull

rm -rf ~/github/lbuild
mkdir -p ~/github/lbuild

cd ~/github/lbuild

../lnav/configure \
    CC="gcc44" \
    CXX="g++44" \
    LDFLAGS="-L${FAKE_ROOT}/lib" \
    PATH="${FAKE_ROOT}/bin:${PATH}" \
    CPPFLAGS="-I${FAKE_ROOT}/include"

make -j2 && strip -o /vagrant/lnav src/lnav
