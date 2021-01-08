#! /bin/bash

run_test ./scripty -n -e ${srcdir}/vt52_curses_input.0 \
    -- ./drive_vt52_curses < /dev/null

on_error_fail_with "single line vt52 did not work?"
