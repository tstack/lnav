#! /bin/sh

sudo apk update && sudo apk upgrade
sudo apk add build-base m4 git zip perl ncurses autoconf automake libtool linux-headers
wget 'https://sourceware.org/git/?p=glibc.git;a=blob_plain;f=misc/sys/queue.h;hb=HEAD' -O queue.h
sudo mv queue.h /usr/include/sys/
