#!/bin/sh

set -Eeuxo pipefail

cd $GITHUB_WORKSPACE
./autogen.sh
mkdir lbuild
cd lbuild
../configure \
    --with-libarchive=/fake.root \
    CFLAGS='-static -g1 -gz=zlib -no-pie -O2' \
    CXXFLAGS='-static -g1 -gz=zlib -U__unused -no-pie -O2' \
    LDFLAGS="-L/fake.root/lib" \
    CPPFLAGS="-I/fake.root/include" \
    LIBS="-L/fake.root/lib -lexecinfo -lssh2 -llzma -lssl -lcrypto -lz" \
    --enable-static \
    PATH="/fake.root/bin:${PATH}"
make -j4
