#! /bin/sh

run_test ./scripty -n -e ${srcdir}/listview_output.0 -- \
    ./drive_listview < /dev/null

on_error_fail_with "listview output does not match?"

run_test ./scripty -- ./drive_listview -t 1 < /dev/null

check_output "Listview didn't move down?" < \
    ${srcdir}/listview_output.1

run_test ./scripty -- ./drive_listview -l 1 < /dev/null

check_output "Listview didn't move right?" < \
    ${srcdir}/listview_output.2

run_test ./scripty -- ./drive_listview -t 1 -l 1 < /dev/null

check_output "Listview didn't move left and right?" < \
    ${srcdir}/listview_output.3

run_test ./scripty -- ./drive_listview -y 1 -r 50 < /dev/null

check_output "Listview doesn't start down one line?" < \
    ${srcdir}/listview_output.4

run_test ./scripty -- ./drive_listview -y 1 -r 50 -h -1 < /dev/null

check_output "Listview isn't shorter?" < \
    ${srcdir}/listview_output.5

run_test ./scripty -- ./drive_listview -y 1 -r 50 -h -1 -t 1 < /dev/null

check_output "Listview didn't move down (2)?" < \
    ${srcdir}/listview_output.6
