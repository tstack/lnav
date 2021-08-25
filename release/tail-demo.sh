#!/usr/bin/env bash

PAUSE_TIME=0.5

while true; do
    logger "lnav is always looking for new log messages"
    sleep ${PAUSE_TIME}
    logger "  and files in directories"
    sleep ${PAUSE_TIME}
    logger "Filters are always applied as new data is loaded in"
    logger "  which is indicated by the 'Not Shown' count"
    logger "bad-message-to-filter"
    sleep ${PAUSE_TIME}
    logger ""
    sleep ${PAUSE_TIME}
    logger "Scroll up to lock the view in place"
    sleep 3
    logger "Data is still being read in the meantime"
done
