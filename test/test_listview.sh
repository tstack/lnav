#! /bin/bash

run_test ./scripty -n -e ${srcdir}/listview_output.0 -- \
    ./drive_listview < /dev/null

on_error_fail_with "listview output does not match?"

run_test ./scripty -n -e ${srcdir}/listview_output.1 -- \
    ./drive_listview -t 1 < /dev/null

on_error_fail_with "listview didn't move down?"

run_test ./scripty -n -e ${srcdir}/listview_output.2 -- \
    ./drive_listview -l 1 < /dev/null

on_error_fail_with "Listview didn't move right?"

run_test ./scripty -n -e ${srcdir}/listview_output.3 -- \
    ./drive_listview -t 1 -l 1 < /dev/null

on_error_fail_with "Listview didn't move left and right?"

run_test ./scripty -n -e ${srcdir}/listview_output.4 -- \
    ./drive_listview -y 1 -r 50 < /dev/null

on_error_fail_with "Listview doesn't start down one line?"

run_test ./scripty -n -e ${srcdir}/listview_output.5 -- \
    ./drive_listview -y 1 -r 50 -h -1 < /dev/null

on_error_fail_with "Listview isn't shorter?"

run_test ./scripty -n -e ${srcdir}/listview_output.6 -- \
    ./drive_listview -y 1 -r 50 -h -1 -t 1 < /dev/null

on_error_fail_with "Listview didn't move down (2)?"
