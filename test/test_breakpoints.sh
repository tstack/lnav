#! /bin/bash

export TZ=UTC
export YES_COLOR=1
export DUMP_CRASH=1

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":breakpoint logging_unittest.cc:259" \
    -c "|lnav-moveto-breakpoint next" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ":breakpoint bad" \
    ${test_dir}/logfile_glog.0
