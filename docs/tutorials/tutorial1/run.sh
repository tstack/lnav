#!/bin/bash

export LNAVSECURE=1
export TERM=xterm-256color

timeout --foreground 5m lnav -I /tutorials/tutorial-lib /tutorials/tutorial1/tutorial1.glog /tutorials/tutorial1/index.md#tutorial-1
