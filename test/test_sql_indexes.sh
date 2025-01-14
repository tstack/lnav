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

rm -f sql_index.err
run_cap_test ${lnav_test} -d sql_index.err -n \
    -c ":goto -1" \
    -c "|find-msg prev access_log cs_referer" \
    -c ";SELECT selection FROM lnav_top_view" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c "|find-msg prev access_log cs_referer" \
    -c ";SELECT selection FROM lnav_top_view" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c "|find-msg prev access_log cs_referer" \
    -c ";SELECT selection FROM lnav_top_view" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ":goto 998" \
    -c "|find-msg prev access_log cs_referer" \
    -c "|find-msg prev access_log cs_referer" \
    -c "|find-msg prev access_log cs_referer" \
    -c ";SELECT selection FROM lnav_top_view" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ";SELECT log_line, log_time, cs_method FROM access_log WHERE cs_referer = 'https://www.zanbil.ir/m/browse/fryer/%D8%B3%D8%B1%D8%AE-%DA%A9%D9%86'" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_shop_access_log.0

run_cap_test grep "vt_next at EOF" sql_index.err

rm -f sql_index.err
run_cap_test ${lnav_test} -d sql_index.err -n \
    -c ":goto 661" \
    -c "|find-msg prev access_log cs_referer" \
    -c ";SELECT selection FROM lnav_views WHERE name = 'log'" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c "|find-msg next access_log cs_referer" \
    -c ";SELECT selection FROM lnav_views WHERE name = 'log'" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c "|find-msg prev access_log cs_referer" \
    -c ";SELECT selection FROM lnav_top_view" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    ${test_dir}/logfile_shop_access_log.0
