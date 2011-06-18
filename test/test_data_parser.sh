#! /bin/bash

for fn in ${top_srcdir}/test/datafile_simple.*; do
    run_test ./drive_data_scanner $fn
    on_error_fail_with "$fn does not match"
done
