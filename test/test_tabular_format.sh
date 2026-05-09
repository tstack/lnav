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

# Builtin scalar columns: thread/duration/src_file/src_line cells should
# flow through process_csv_cell into log_thread_id / log_duration /
# log_src_file / log_src_line.  Row 3's `-` thread placeholder must
# null-out log_thread_id; durations come back in seconds (dur_ms /
# elf_duration_divisor).
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_line, log_thread_id, log_duration, log_src_file, log_src_line FROM all_logs ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular.csv

# Detail block: covers two cases —
#  - declared columns the windows_event_log format does NOT reference in
#    its line-format (Id, Version): these go through process_csv_cell's
#    declared branch and use the format's declared kind (integer).
#  - header columns the format doesn't declare at all (CustomTag,
#    SomeNumber, Floaty): synthesized via get_value_meta() with kinds
#    derived from separated_string::cell_kind.  Floaty's `3.14`
#    round-trips through VALUE_FLOAT and renders with %lf precision,
#    matching the JSON path's behavior for typed numerics.
# Both end up in the rewritten subline's detail block; `log_text` returns
# the full multi-line content per row.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_text FROM all_logs ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_win_events_extras.csv

# Detail block default render: scan_tabular emits continued sub-loglines
# for every detail-block cell so the listview can navigate them.  `lnav
# -n` prints each sub-line, so both declared-not-consumed columns and
# extras appear as `  name: value` rows beneath each event.
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_win_events_extras.csv

# CSV `""` escapes: a body cell `"User said ""hi"""` and an opid cell
# `"op""1"` should both round-trip to their canonical (single-quote)
# form in the rendered line, the log_body column, and downstream
# bookkeeping (opid grouping in all_opids).
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_win_events_quotes.csv

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_opid, log_body FROM all_logs ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_win_events_quotes.csv

run_cap_test ${lnav_test} -n \
    -c ";SELECT opid, total FROM all_opids ORDER BY opid" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_win_events_quotes.csv

# log_extra_fields column: undeclared header columns (CustomTag,
# SomeNumber, Floaty) get rolled up into a JSON object on the
# per-format vtab.  Type fidelity comes from separated_string's
# cell_kind classifier — strings stay strings, integers numeric, and
# floats round-trip through VALUE_FLOAT.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_extra_fields FROM windows_event_log ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_win_events_extras.csv

# Same column drilled with json_extract(): proves the emitted JSON is
# well-formed and that SQLite's json1 sees the typed values back out
# (string, integer, float).
run_cap_test ${lnav_test} -n \
    -c ";SELECT json_extract(log_extra_fields, '\$.CustomTag') AS tag, json_extract(log_extra_fields, '\$.SomeNumber') AS num, json_extract(log_extra_fields, '\$.Floaty') AS flt FROM windows_event_log ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_win_events_extras.csv

# Quote escaping in extras: an undeclared column whose name and value
# both contain CSV `""` escapes (`Tag"Key`, `She said "hi"`) must come
# back through log_extra_fields as a JSON object with the embedded
# quotes properly `\"`-escaped, and an empty cell must become JSON
# null rather than an empty string.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_extra_fields FROM windows_event_log ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_win_events_extras_quotes.csv

run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_win_events_extras_quotes.csv

# Format with no extras: tabular_test_log declares every header
# column, so tlf_extra_count is 0 and log_extra_fields stays NULL.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_line, log_extra_fields FROM tabular_test_log ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular.csv

# Multi-line cells: a quoted CSV value that contains literal newlines
# must be glued back into a single logline.  The fixture's middle row
# spans three physical lines; we expect three loglines total, with the
# warn row's log_body carrying the full multi-line text.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_tabular_multiline.csv

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_line, log_level, log_body FROM all_logs ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_tabular_multiline.csv

run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_win_events_multiline.csv

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_level, log_body, log_raw_text FROM all_logs ORDER BY log_line" \
    -c ':write-json-to -' \
    ${test_dir}/logfile_win_events_multiline.csv
