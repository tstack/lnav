#!/bin/bash


if test x"${TRAVIS_BUILD_DIR}" == x""; then
    exit 0
fi

cp lnav ${TRAVIS_BUILD_DIR}/
cd ${TRAVIS_BUILD_DIR}

VERSION=`git describe --tags`
zip lnav-${VERSION}-linux-64bit.zip lnav
