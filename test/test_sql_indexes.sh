#! /bin/bash

export TZ=UTC
export YES_COLOR=1

# XXX sqlite reports different results for the "detail" column, so we
# have to rewrite it.
run_cap_test ${lnav_test} -n \
    -c ";EXPLAIN QUERY PLAN SELECT * FROM access_log WHERE log_path GLOB '*/logfile_access_log.*'" \
    -c ";SELECT \$id, \$parent, replace(\$detail, 'SCAN TABLE', 'SCAN')" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT *,log_unique_path FROM access_log WHERE log_path GLOB '*/logfile_access_log.*'" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";EXPLAIN QUERY PLAN SELECT * FROM all_logs WHERE log_format = 'access_log'" \
    -c ";SELECT \$id, \$parent, replace(\$detail, 'SCAN TABLE', 'SCAN')" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT *,log_format FROM all_logs WHERE log_format = 'access_log'" \
    ${test_dir}/logfile_access_log.* \
    ${test_dir}/logfile_procstate.0

run_cap_test ${lnav_test} -n \
    -c ";EXPLAIN QUERY PLAN SELECT * FROM all_logs WHERE log_level < 'error'" \
    -c ";SELECT \$id, \$parent, replace(\$detail, 'SCAN TABLE', 'SCAN')" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM all_logs WHERE log_level < 'error'" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM all_logs WHERE log_level <= 'error'" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM all_logs WHERE log_level >= 'error'" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM all_logs WHERE log_level > 'error'" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM all_logs WHERE log_line <= 20" \
    ${test_dir}/logfile_access_log.*
