#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -W -C \
    -I ${test_dir}/bad-config-json

if test x"$HAVE_SQLITE3_ERROR_OFFSET" != x""; then
    run_cap_test env LC_ALL=C ${lnav_test} -W -C \
        -I ${test_dir}/bad-config
fi

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";select * from leveltest_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_leveltest.0
