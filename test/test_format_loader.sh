#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -C \
    -I ${test_dir}/bad-config-json

run_cap_test env LC_ALL=C ${lnav_test} -C \
    -I ${test_dir}/bad-config

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";select * from leveltest_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_leveltest.0
