# Release

This directory contains the [Makefile](Makefile) and scripts used to build the
binaries for a release.

## Manual Testing

* `lnav -e 'make check'`
* `lnav Makefile`
* `lnav Makefile:22`
* `lnav -c ":goto 11" Makefile`

## Process

1. `git tag v0.00.0`
2. `git push origin tag v0.00.0`
3. Make sure builds are working on:
    * snapcraft.org
    * readthedocs.org
