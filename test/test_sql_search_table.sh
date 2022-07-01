#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -n \
    -c ';SELECT * FROM procstate_procs' \
    ${test_dir}/logfile_procstate.0
