#!/bin/sh

set -Eeuxo pipefail

cd $GITHUB_WORKSPACE
./autogen.sh
./configure
make -j4
