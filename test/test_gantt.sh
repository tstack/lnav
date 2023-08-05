#! /bin/bash

export YES_COLOR=1

export TZ=UTC

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view gantt' \
    ${test_dir}/logfile_bro_http.log.0
