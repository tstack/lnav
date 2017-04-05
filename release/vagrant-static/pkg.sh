#! /usr/bin/env bash

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
        git \
        centos-release-scl \
        perl-Data-Dumper
    sudo yum install -y "devtoolset-4-gcc*"
else
    pkg install -y wget git gcc m4
fi
