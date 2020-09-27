#! /bin/sh

sudo apk update && sudo apk upgrade
sudo apk add build-base m4 git zip perl ncurses autoconf automake libtool linux-headers
