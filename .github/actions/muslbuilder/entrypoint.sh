#!/bin/sh

set -Eeuxo pipefail

if [ -z ${GITHUB_WORKSPACE:-} ]; then
    git clone --depth 1 https://github.com/tstack/lnav.git
    cd lnav
else
    cd ${GITHUB_WORKSPACE}
fi

./autogen.sh
mkdir lbuild
cd lbuild
../configure \
    --with-libarchive=/fake.root \
    CFLAGS='-static -g1 -gz=zlib -no-pie -O2' \
    CXXFLAGS='-static -g1 -gz=zlib -U__unused -no-pie -O2' \
    LDFLAGS="-L/fake.root/lib" \
    CPPFLAGS="-I/fake.root/include -I/fake.root/include/ncursesw" \
    LIBS="-L/fake.root/lib -lssh2 -llzma -lssl -lcrypto -lz -llz4" \
    --enable-static \
    PATH="/fake.root/bin:${PATH}"
make -j2
