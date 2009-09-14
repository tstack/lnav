#! /bin/sh

run_test ./scripty -n -r ${srcdir}/vt52_curses_input.0 \
    -e ${srcdir}/vt52_curses_output.0 -- ./drive_vt52_curses < /dev/null

on_error_fail_with "single line vt52 did not work?"

run_test ./scripty -n -r ${srcdir}/vt52_curses_input.1 \
    -e ${srcdir}/vt52_curses_output.1 -- ./drive_vt52_curses -y 5 < /dev/null

on_error_fail_with "vt52 doesn't maintain past lines?"
