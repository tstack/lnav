#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -n \
    -c ";EXPLAIN QUERY PLAN SELECT * FROM syslog_log WHERE log_path GLOB '*/logfile_syslog.*'" \
    ${test_dir}/logfile_syslog.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM syslog_log WHERE log_path GLOB '*/logfile_syslog.*'" \
    ${test_dir}/logfile_syslog.*

