#!/bin/sh

OS=$(uname -s)
if test x"${OS}" != x"FreeBSD"; then
    sudo yum install -y \
        zip \
        unzip \
        m4 \
        autoconf \
        automake \
        ncurses \
        ncurses-devel \
        ncurses-static \
        git \
        centos-release-scl \
        perl-Data-Dumper \
        patch \
        wget
    sudo yum install -y "devtoolset-9-gcc*"
else
    pkg install -y wget git m4 bash autoconf automake sqlite3 gmake
fi
