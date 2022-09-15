#!/bin/bash

export LNAVSECURE=1
export TERM=xterm-256color

timeout --foreground --kill-after=30s 5m lnav \
    -d "/tmp/$(echo "tutorial1."$(date "+%Y-%m-%dT%H-%M-%S")".$$.log")" \
    -I /tutorials/tutorial-lib \
    /tutorials/tutorial1/tutorial1.glog \
    /tutorials/tutorial1/index.md#tutorial-1

if [ $? = 124 ]; then
  echo "error: reached connection time limit, reconnect if you're not a bot."
else
  echo "Thanks for trying out lnav!  Have a nice day!"
fi
