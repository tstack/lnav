#! /bin/bash

export YES_COLOR=1

export TZ=UTC

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view gantt' \
    ${test_dir}/logfile_generic.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view gantt' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view gantt' \
    -c ':filter-in CdysLK1XpcrXOpVDuh' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view gantt' \
    -c ':filter-out CdysLK1XpcrXOpVDuh' \
    ${test_dir}/logfile_bro_http.log.0
