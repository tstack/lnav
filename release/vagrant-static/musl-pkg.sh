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
    libexecinfo-dev \
    libexecinfo-static \
    libtool \
    linux-headers
