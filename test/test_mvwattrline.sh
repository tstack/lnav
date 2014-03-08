#! /bin/bash

run_test ./scripty -n -e ${srcdir}/mvwattrline_output.0 -- \
    ./drive_mvwattrline < /dev/null

on_error_fail_with "mvwattrline does not work"
