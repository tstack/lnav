#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -n \
    -c ";from access_log | take 1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";from access_log | take abc" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";from bro_http_log | stats.hist bro_host slice:'1m'" \
    ${test_dir}/logfile_bro_http.log.0
