#! /bin/bash

export TZ=UTC
export YES_COLOR=1
export HOME="./tabular-sessions"
unset XDG_CONFIG_HOME
rm -rf "./tabular-sessions"
mkdir -p $HOME

# Default render: header detected, level styling applied to rows, body
# field surfaces in the message column.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_tabular.csv

# Timestamps and levels: round-trip the parsed values through SQL so a
# regression in ingest_timestamp or convert_level fails fast.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_time, log_level, log_body FROM all_logs ORDER BY log_time" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular.csv

# Opid + thread parse with `-` placeholder: rows 2 and 4 use `-` for
# opid/thread respectively and should have NULL log_opid / no thread
# entry, while rows with real values show through.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_time, log_opid FROM all_logs ORDER BY log_time" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular.csv

# all_opids view: verifies record_opid + finalize_line wired the
# duration through to the opid map.  op1 spans rows 0+2 (duration of
# the second row added) and op2 spans rows 3+4.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT opid, total, errors, warnings FROM all_opids ORDER BY opid" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular.csv

# Numeric ingest: per-format tabular vtab exposes the parsed cpu_pct
# and queue columns.  This catches regressions in ingest_numeric_value
# (gating on identifier/foreign-key, kind dispatch).
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_time, cpu_pct, queue FROM tabular_test_log ORDER BY log_time" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular.csv

# Opid synthesis: format declares no opid-field, but an opid
# `description` block names client_ip + user as the synthesis source.
# Rows with the same (client_ip, user) pair should collapse to one
# all_opids entry; alice gets 2 messages, bob gets 2.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT total, errors, warnings FROM all_opids ORDER BY total DESC, errors" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular_synth.csv
