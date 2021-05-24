#! /bin/sh

sudo apk update && sudo apk upgrade
sudo apk add \
    build-base \
    binutils \
    m4 \
    git \
    zip \
    perl \
    ncurses \
    autoconf \
    automake \
    elfutils \
    elfutils-dev \
    libdwarf \
    libdwarf-dev \
    libdwarf-static \
    libelf-static \
    libexecinfo-dev \
    libexecinfo-static \
    libtool \
    linux-headers
