#!/bin/bash

export LNAVSECURE=1
export TERM=xterm-256color

timeout --foreground --kill-after=30s 10m lnav \
    -d "/tmp/$(echo "playground."$(date "+%Y-%m-%dT%H-%M-%S")".$$.log")" \
    /tutorials/playground/logs \
    /tutorials/playground/text \
    /tutorials/playground/index.md#playground

if [ $? = 124 ]; then
  echo "error: reached connection time limit, reconnect if you're not a bot."
else
  echo "Thanks for trying out lnav!  Have a nice day!"
fi
