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

run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) FROM access_log WHERE cs_uri_stem = '/vmw/cgi/tramp'" \
    -c ";SELECT count(*) FROM access_log WHERE cs_uri_stem = '/vmw/cgi/tramp'" \
    ${test_dir}/logfile_access_log.0

# A scan that stops early (a LIMIT) and started away from the indexed lines
# must not leave the index claiming the lines in between: the count should be
# all 14 POSTs, not just the ones the LIMITed queries saw.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line FROM access_log WHERE cs_method = 'POST' ORDER BY log_line DESC LIMIT 1" \
    -c ";SELECT log_line FROM access_log WHERE cs_method = 'POST' ORDER BY log_line ASC LIMIT 1" \
    -c ";SELECT count(*) AS indexed FROM access_log WHERE cs_method = 'POST'" \
    ${test_dir}/logfile_shop_access_log.0

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

grep "vt_next at EOF" sql_index.err > sql_index_vt_next.err
run_cap_test sed -e 's/^.*\(vt_next at EOF.*\)$/\1/g' sql_index_vt_next.err

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

# A line that the column index gives, but that another constraint rules out,
# is skipped by moving to the next indexed line, not the next line number.
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) FROM access_log WHERE c_ip = '192.168.202.254'" \
    -c ";SELECT (SELECT group_concat(log_line) FROM access_log WHERE c_ip = '192.168.202.254' AND log_level = 'error') AS err, (SELECT group_concat(log_line) FROM access_log WHERE c_ip = '192.168.202.254' AND log_level = 'info') AS info, (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE c_ip = '192.168.202.254' AND log_level = 'info' ORDER BY log_line DESC)) AS info_desc" \
    ${test_dir}/logfile_access_log.0

# Ranges of log_line apply to a reverse scan the right way around.
run_cap_test ${lnav_test} -n \
    -c ";SELECT (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE log_line >= 1 ORDER BY log_line DESC)) AS ge, (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE log_line > 0 ORDER BY log_line DESC)) AS gt, (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE log_line <= 1 ORDER BY log_line DESC)) AS le, (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE log_line < 2 ORDER BY log_line DESC)) AS lt" \
    ${test_dir}/logfile_access_log.0

# Bounds at the ends of the 64-bit range don't overflow, in either direction.
run_cap_test ${lnav_test} -n \
    -c ";SELECT (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line <= 9223372036854775807 ORDER BY log_line ASC)) AS asc_le_max, (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line > 9223372036854775807 ORDER BY log_line ASC)) AS asc_gt_max, (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line >= -9223372036854775808 ORDER BY log_line ASC)) AS asc_ge_min, (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line < -9223372036854775808 ORDER BY log_line ASC)) AS asc_lt_min, (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line <= 9223372036854775807 ORDER BY log_line DESC)) AS desc_le_max, (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line > 9223372036854775807 ORDER BY log_line DESC)) AS desc_gt_max, (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line >= -9223372036854775808 ORDER BY log_line DESC)) AS desc_ge_min, (SELECT count(*) FROM (SELECT log_line FROM access_log WHERE log_line < -9223372036854775808 ORDER BY log_line DESC)) AS desc_lt_min" \
    ${test_dir}/logfile_access_log.0

# So do ranges of time.
run_cap_test ${lnav_test} -n \
    -c ";SELECT (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE log_time >= '2009-07-20 22:59:29.000000' ORDER BY log_line DESC)) AS time_ge, (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE log_time <= '2009-07-20 22:59:26.000000' ORDER BY log_line DESC)) AS time_le, (SELECT group_concat(log_line) FROM (SELECT log_line FROM access_log WHERE log_time_msecs >= 1248130769000 ORDER BY log_line DESC)) AS msecs_ge" \
    ${test_dir}/logfile_access_log.0

# A log_level constraint that is pushed down into the table has to agree with
# the loglevel collation that SQLite checks the rows with, so the same
# condition finds the same rows either way.  Concatenating an empty string
# keeps the constraint from being pushed down.
run_cap_test ${lnav_test} -n \
    -c ";SELECT (SELECT group_concat(log_line) FROM access_log WHERE log_level >= 'deprecation') AS pushed_ge, (SELECT group_concat(log_line) FROM access_log WHERE (log_level || '') >= 'deprecation' COLLATE loglevel) AS collation_ge, (SELECT group_concat(log_line) FROM access_log WHERE log_level = 'fail') AS pushed_eq, (SELECT group_concat(log_line) FROM access_log WHERE (log_level || '') = 'fail' COLLATE loglevel) AS collation_eq" \
    ${test_dir}/logfile_access_log.0

# A time range narrows the scan even when the c_ip index isn't built yet:
# the scan covers the window instead of the whole log to fill the index, and
# leaves the index alone, so the later count still sees every row.  Pushed
# down or not, and with the index built or not, the rows are the same.
run_cap_test ${lnav_test} -n \
    -c ";SELECT (SELECT group_concat(log_line) FROM access_log WHERE c_ip = '66.249.66.194' AND log_time >= '2019-01-22 00:27:00' AND log_time < '2019-01-22 00:27:30') AS timed, (SELECT group_concat(log_line) FROM access_log WHERE (c_ip || '') = '66.249.66.194' AND log_time >= '2019-01-22 00:27:00' AND log_time < '2019-01-22 00:27:30') AS unpushed, (SELECT count(*) FROM access_log WHERE c_ip = '66.249.66.194') AS total, (SELECT group_concat(log_line) FROM access_log WHERE c_ip = '66.249.66.194' AND log_time >= '2019-01-22 00:27:00' AND log_time < '2019-01-22 00:27:30') AS indexed" \
    ${test_dir}/logfile_shop_access_log.0

# log_file_line bounds narrow the scan to the rows that can hold those lines
# of each file the path constraints allow.  A unary plus keeps the constraint
# from being pushed down, so the same condition finds the same rows either
# way: across files, for JSON messages rendered into several lines, for a
# range inside a multi-line message (no rows start there), in either
# direction, with a bound on only one side, and with other constraints on the
# rows.
run_cap_test ${lnav_test} -n \
    -c ";EXPLAIN QUERY PLAN SELECT * FROM all_logs WHERE log_path GLOB '*/logfile_json.json' AND log_file_line BETWEEN 3 AND 8" \
    -c ";SELECT \$id, \$parent, replace(\$detail, 'SCAN TABLE', 'SCAN')" \
    ${test_dir}/logfile_json.json

run_cap_test ${lnav_test} -n \
    -c ";SELECT (SELECT group_concat(log_line) FROM all_logs WHERE log_file_line BETWEEN 2 AND 3) AS pushed_files, (SELECT group_concat(log_line) FROM all_logs WHERE +log_file_line BETWEEN 2 AND 3) AS unpushed_files, (SELECT group_concat(log_line) FROM all_logs WHERE log_path GLOB '*/logfile_json.json' AND log_file_line BETWEEN 12 AND 14) AS pushed_json, (SELECT group_concat(log_line) FROM all_logs WHERE log_path GLOB '*/logfile_json.json' AND +log_file_line BETWEEN 12 AND 14) AS unpushed_json, (SELECT group_concat(log_line) FROM all_logs WHERE log_path GLOB '*/logfile_postgres.1' AND log_file_line BETWEEN 3 AND 40) AS pushed_inside, (SELECT group_concat(log_line) FROM (SELECT log_line FROM all_logs WHERE log_file_line BETWEEN 1 AND 41 ORDER BY log_line DESC)) AS pushed_desc, (SELECT group_concat(log_line) FROM (SELECT log_line FROM all_logs WHERE +log_file_line BETWEEN 1 AND 41 ORDER BY log_line DESC)) AS unpushed_desc, (SELECT group_concat(log_line) FROM all_logs WHERE log_file_line = 2 AND log_line IN (1, 2, 3, 40, 41)) AS pushed_in, (SELECT group_concat(log_line) FROM all_logs WHERE log_file_line > 2.5 AND log_file_line <= 4) AS pushed_fraction, (SELECT group_concat(log_line) FROM all_logs WHERE log_file_line BETWEEN 5 AND 4) AS pushed_empty, (SELECT group_concat(log_line) FROM all_logs WHERE log_file_line BETWEEN NULL AND 5) AS pushed_null, (SELECT group_concat(log_line) FROM all_logs WHERE log_file_line > 30) AS pushed_gt, (SELECT group_concat(log_line) FROM all_logs WHERE +log_file_line > 30) AS unpushed_gt, (SELECT group_concat(log_line) FROM all_logs WHERE log_file_line <= 3) AS pushed_le, (SELECT group_concat(log_line) FROM all_logs WHERE +log_file_line <= 3) AS unpushed_le" \
    ${test_dir}/logfile_json.json \
    ${test_dir}/logfile_postgres.1 \
    ${test_dir}/logfile_access_log.0
