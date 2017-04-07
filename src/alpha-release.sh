#!/bin/bash


if test x"${TRAVIS_BUILD_DIR}" == x""; then
    exit 0
fi

cp lnav ${TRAVIS_BUILD_DIR}/
cd ${TRAVIS_BUILD_DIR}

ldd lnav
VERSION=`git describe --tags`
zip lnav-${VERSION}-linux-64bit.zip lnav
