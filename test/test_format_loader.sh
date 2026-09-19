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

# A name used by more than one group with "(?J)" takes its value from
# whichever alternative matched.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";select log_level, comp, log_body from dupnames_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_dupnames.0

# The "json" flag is deprecated in favor of "file-type": "json", but it is
# still accepted and lnav does not warn about it, so nothing else would notice
# if it stopped setting the file type.  Every other format has been converted;
# this one deliberately has not.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";select log_format, log_level, log_body from json_deprecated_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json_deprecated.json
