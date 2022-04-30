#!/usr/bin/env bash

srcdir="$1"
builddir="$2"

for fname in "${srcdir}"/expected/*.out; do
  stem=$(basename "$fname" | sed -e 's/.out$//')

  if ! test -f "${builddir}/$stem.cmd"; then
    echo "removing $fname"
    guilt rm "$fname"
    echo "removing ${srcdir}/expected/${stem}.err"
    guilt rm "${srcdir}/expected/${stem}.err"
  fi
done
