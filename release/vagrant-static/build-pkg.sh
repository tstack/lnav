#!/usr/bin/env bash

VERSION=$1

fpm --verbose \
    -s zip \
    -t rpm \
    -n "lnav" \
    -v "$VERSION" \
    -p /vagrant/ \
    -a native \
    --url https://lnav.org \
    -m 'timothyshanestack@gmail.com' \
    --description 'A log file viewer and analyzer for the terminal' \
    --license BSD-2-Clause \
    --category 'System/Monitoring' \
    /vagrant/lnav-linux.zip

fpm --verbose \
    -s zip \
    -t deb \
    -n "lnav" \
    -v "$VERSION" \
    -p /vagrant/ \
    -a native \
    --url https://lnav.org \
    -m 'timothyshanestack@gmail.com' \
    --description 'A log file viewer and analyzer for the terminal' \
    --license BSD-2-Clause \
    /vagrant/lnav-linux.zip
