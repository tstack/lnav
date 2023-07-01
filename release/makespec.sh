#!/usr/bin/env bash

DEFAULT_VERSION=$(head -1 ../configure.ac | cut -d, -f2)
DEFAULT_VERSION=${DEFAULT_VERSION#[}
DEFAULT_VERSION=${DEFAULT_VERSION%]}

if [ "$GITHUB_REF_TYPE" == 'tag' ]; then
    export LNAV_VERSION_NUMBER=${GITHUB_REF#refs/tags/v}
else
    export LNAV_VERSION_NUMBER="${DEFAULT_VERSION}^"$(date +%Y%m%d).git$(git rev-parse --short HEAD)
fi

sed -e "s/@@LNAV_VERSION_NUMBER@@/${LNAV_VERSION_NUMBER}/g"
