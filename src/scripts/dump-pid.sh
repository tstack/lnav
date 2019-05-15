#!/bin/sh

IN_PID=`cat`

if test "${IN_PID}" -gt 0 > /dev/null 2>&1 && \
    kill -0 $IN_PID > /dev/null 2>&1; then
    echo "== ps =="
    ps uewww -p $IN_PID
    echo "== lsof =="
    lsof -p $IN_PID
else
    echo "error: inaccessible process -- $IN_PID" > /dev/stderr
fi
