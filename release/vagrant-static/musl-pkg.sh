#!/bin/sh

sudo apk update && sudo apk upgrade
sudo apk add \
    build-base \
    binutils \
    m4 \
    git \
    zip \
    perl \
    ncurses \
    ncurses-dev \
    autoconf \
    automake \
    elfutils \
    elfutils-dev \
    libelf-static \
    libexecinfo-dev \
    libexecinfo-static \
    libtool \
    libunwind \
    libunwind-dev \
    libunwind-static \
    linux-headers
