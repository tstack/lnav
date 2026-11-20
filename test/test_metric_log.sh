#! /bin/bash

export TZ=UTC
export YES_COLOR=1
export LC_ALL=C
export HOME="./metric-sessions"
unset XDG_CONFIG_HOME
rm -rf "./metric-sessions"
mkdir -p $HOME

# Single metric CSV: verify detection + rendered output.
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_metrics.csv

# Per-row metadata: each data row becomes a LEVEL_STATS logline under
# the built-in metric_log format.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_format, log_level, log_time FROM all_logs" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv

# Cross-file merge: two CSVs with identical timestamps.  After the
# index dedup, each timestamp appears as a single visible line with
# columns from both files folded in.
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# metrics vtab: long-format view across a single file.  6 rows × 3
# columns = 18 samples.
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv

# metrics vtab: filter by metric name across both files, joining the
# long-format samples.
run_cap_test ${lnav_test} -n \
    -c ";SELECT source, metric, value FROM all_metrics WHERE metric IN ('cpu_pct', 'queue_depth') ORDER BY log_time, source, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# metrics vtab: aggregate query across both files.
run_cap_test ${lnav_test} -n \
    -c ";SELECT metric, count(*) AS n, round(min(value), 2) AS lo, round(max(value), 2) AS hi FROM all_metrics GROUP BY metric ORDER BY metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# metrics vtab: suffixed values (e.g. "20.0KB") — the `value` column
# should expose the numeric expansion while `raw_value` preserves the
# original cell text.
run_cap_test ${lnav_test} -n \
    -c ";SELECT metric, value, raw_value FROM all_metrics ORDER BY log_time, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics3.csv

# metrics vtab: `raw_value` uses the measure_with_units collator so
# ORDER BY sorts by magnitude rather than lexicographically (e.g.
# "1.5MB" > "20.0KB").
run_cap_test ${lnav_test} -n \
    -c ";SELECT metric, raw_value FROM all_metrics ORDER BY raw_value" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics3.csv

# Excel-flavor `sep=<ch>` hint on the first line overrides the
# default comma delimiter (here, semicolon) so the rest of the file
# parses as a regular metric CSV.
run_cap_test ${lnav_test} -n \
    -c ";SELECT metric, value FROM all_metrics ORDER BY log_time, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_sep.csv

# Grafana-style CSV export: UTF-8 BOM, CRLF line endings, quoted
# header names containing parentheses + dots, plain-space datetime
# first column.  Exercises the line_buffer BOM skip and
# separated_string's quote unwrapping in a single file.
run_cap_test ${lnav_test} -n \
    -c ";SELECT metric, value FROM all_metrics ORDER BY log_time, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_grafana.csv

# Header with `""`-escaped double quotes inside a quoted column name
# (common in Grafana/PromQL exports) must parse as a single column,
# not split on commas that live inside the escaped portion.
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) AS rows, count(DISTINCT metric) AS metrics FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_quoted.csv

# metrics vtab: log_line — every sample at a shared timestamp maps to
# the same visible row, even when the samples came from sibling
# metric files.  The first three timestamps (log_line 0..2) each span
# both files, so they should produce 5 samples per line.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, count(*) AS n FROM all_metrics GROUP BY log_line ORDER BY log_line LIMIT 3" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# metrics vtab: active LOG-view filters narrow the metric table too.
# Hiding one row via :filter-out drops that row's samples and renumbers
# log_line in the filtered space (5 visible rows × 3 metrics = 15).
run_cap_test ${lnav_test} -n \
    -c ":filter-out 99.99" \
    -c ";SELECT count(*) AS n FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv

# metrics vtab: log_line after filter is compact in filtered-index
# space.  The hidden row at T=25 is gone entirely; surviving rows are
# numbered 0..4 with 3 samples each.
run_cap_test ${lnav_test} -n \
    -c ":filter-out 99.99" \
    -c ";SELECT log_line, metric, value FROM all_metrics ORDER BY log_line, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv

# metrics vtab: filter-out applied to a row in one metric file must
# also drop that row's samples from the cross-file fan-out.  When
# file1's T=25 row is hidden but file2's T=25 row survives, the
# visible log_line at T=25 should expose only file2's samples — file1
# must not rejoin via the sibling walk.
run_cap_test ${lnav_test} -n \
    -c ":filter-out 99.99" \
    -c ";SELECT log_line, source, metric FROM all_metrics WHERE log_time = '2026-04-14T10:00:25.000000' ORDER BY source, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# metrics vtab: marking one metric row fans the mark out to every
# sibling sample at that timestamp, across files.
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs SET log_mark = 1 WHERE log_line = 0" \
    -c ";SELECT source, metric, log_mark FROM all_metrics WHERE log_line = 0 ORDER BY source, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# Session persistence regression: marks across multiple metric files
# survive save-session + load-session.  Earlier, only the lead
# sibling's hash made it into the bookmarks DB, leaving the
# suppressed-sibling files unmarked after reload.
run_cap_test ${lnav_test} -nq \
    -c ":reset-session" \
    -c ";UPDATE all_logs SET log_mark = 1 WHERE log_line = 0" \
    -c ":save-session" \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";SELECT source, metric, log_mark FROM all_metrics WHERE log_line = 0 ORDER BY source, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# Sticky marks must fan out to sibling metric rows too, and survive a
# save-session / load-session round trip.  Regression for a bug where
# only BM_USER was fanned out in text_mark.
run_cap_test ${lnav_test} -nq \
    -c ":reset-session" \
    -c ";UPDATE all_logs SET log_sticky_mark = 1 WHERE log_line = 0" \
    -c ":save-session" \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";SELECT log_line, log_sticky_mark FROM all_logs WHERE log_line < 3" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# Field overlay: when row-details is enabled, every metric column
# (lead's *and* suppressed siblings') shows up in the "Known message
# fields" list so show/hide and chart actions can target them.
run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_top_view SET options = json_set(options, '\$.row-details', 'show')" \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# :hide-fields drops a metric column from the composed LOG-view line
# but leaves the others intact.
run_cap_test ${lnav_test} -n \
    -c ":hide-fields metrics_log.cpu_pct" \
    ${test_dir}/logfile_metrics.csv

# :show-fields restores a previously hidden metric column.
run_cap_test ${lnav_test} -n \
    -c ":hide-fields metrics_log.cpu_pct" \
    -c ":show-fields metrics_log.cpu_pct" \
    ${test_dir}/logfile_metrics.csv

# :hide-fields fans out across every open metric file that carries
# the column — here cpu_pct lives only in logfile_metrics.csv, so
# hiding it drops it from the merged row while logfile_metrics2's
# columns stay visible.
run_cap_test ${lnav_test} -n \
    -c ":hide-fields metrics_log.cpu_pct" \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# Session persistence: the hidden-field state survives save-session +
# load-session so a user-hidden metric column stays hidden on reload.
run_cap_test ${lnav_test} -nq \
    -c ":reset-session" \
    -c ":hide-fields metrics_log.cpu_pct" \
    -c ":save-session" \
    ${test_dir}/logfile_metrics.csv

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    ${test_dir}/logfile_metrics.csv

# :reset-session clears user hide state for metric columns — a hidden
# column becomes visible again even if it was persisted in the
# session.  Regression for the static registry outliving reset.
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":reset-session" \
    ${test_dir}/logfile_metrics.csv

# :hide-fields in the reverse direction — hiding a column that only
# lives in file2 (queue_depth) should drop it from the merged row
# while file1's columns stay visible.
run_cap_test ${lnav_test} -n \
    -c ":hide-fields metrics_log.queue_depth" \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# Negative: a CSV whose data row has more (or fewer) cells than the
# header triggers `scan_error` during detection; the file must not be
# detected as a metrics_log so `all_metrics` stays empty.
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) AS n FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_colmismatch.csv

# Negative: an unterminated quote on the header line is captured by
# `separated_string` as a single field, which fails the "too few
# columns" check in metric detection — file must not be detected as a
# metrics_log.
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) AS n FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_badquote.csv

# Negative: a CSV whose *later* data row has the wrong field count
# (detection succeeded on row 0 before the bad row appears).  Pins
# the current behavior for the post-specialize `scan_int` error path.
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_metrics_colmismatch_late.csv

# Search in the LOG view matches against the composed
# `<ts> <col>=<value>` line the user sees, not the raw CSV bytes.
# Regression: `RF_RAW` returned raw bytes so searching for a column
# name (which lives only in the rendered text) missed every row.
# `:prev-mark search` from end-of-file should land on row 0 when the
# pattern matches the composed line.
run_cap_test ${lnav_test} -n \
    -c "/cpu_pct" \
    -c ":prev-mark search" \
    -c ";SELECT selection FROM lnav_views WHERE name='log'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv

# `:set-file-timezone` after the file has been specialized triggers a
# reindex with `lf_specialized` still set and `lf_index` cleared.
# Regression: the first scan call landed in `scan_int` with an empty
# `dst`, reading past-the-end and assigning every row the same bogus
# timestamp (metric dedup then collapsed them into one visible row).
# Now every row should render with its own LA→UTC-converted time.
run_cap_test ${lnav_test} -n \
    -c ":set-file-timezone America/Los_Angeles" \
    -c ";SELECT log_line, log_time FROM all_logs ORDER BY log_line" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_grafana.csv

# Negative: a CSV whose first column isn't a timestamp-shaped header
# must not be detected as a metrics_log — the file should open as
# plain text and all_metrics should be empty.
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) AS n FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_not_metrics.csv

# Negative: a CSV with only a timestamp column (no data columns) is
# rejected as "too few columns for a metric CSV".
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) AS n FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_bad.csv

# Negative: a CSV whose first data row has no numeric fields (only
# text) must not be detected as a metrics_log — the "metric row has
# no numeric fields" check rejects it before specialization so the
# file falls through to plain text and all_metrics stays empty.
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) AS n FROM all_metrics" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_textonly.csv

# Mixed text + numeric columns: detection succeeds because at least
# one column (cpu_pct) is numeric.  During annotation, the text
# columns (host, region) are exposed as VALUE_TEXT fields so they
# render in the LOG view next to the numeric value.
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_metrics_mixed.csv

# Mixed file via all_metrics: only numeric samples are emitted; the
# text columns appear with NULL value because the long-format vtab
# is for numeric series.  Confirms the text-cell annotate path does
# not pollute numeric stats.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, metric, value FROM all_metrics ORDER BY log_line, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_mixed.csv

# Out-of-order timestamps: with lf_time_ordered=false the metrics
# format no longer triggers lnav's time-skew fixup that rewrites
# later out-of-order rows to the previous row's time.  Each row
# should keep its own parsed timestamp, and the LOG view should
# render them sorted by time regardless of file order.
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_metrics_unordered.csv

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_time, metric, value FROM all_metrics ORDER BY log_line, metric, value" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics_unordered.csv

# Negative: spectrogram on an unknown field name produces a clear
# error rather than hanging or crashing.
run_cap_test ${lnav_test} -n \
    -c ":spectrogram nonexistent_column" \
    ${test_dir}/logfile_metrics.csv

# Spectrogram on a metric column from the lead file.  Exercises the
# metric-aware sibling walk and the update_stats fix that skips
# ignored (header) rows when probing the time range.
run_cap_test ${lnav_test} -n \
    -c ":spectrogram mem_mb" \
    ${test_dir}/logfile_metrics.csv

# Spectrogram on a metric column that lives in a non-lead sibling
# file.  Regression for the case where the column belongs to the
# second metric file and rows are suppressed from the filtered index.
run_cap_test ${lnav_test} -n \
    -c ":spectrogram queue_depth" \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# A JSON log with one metric sample per line, named in the data.  The line
# shows the format's display entries in order, with blanks for a metric that
# has no sample at that time, and a counter's change instead of its total.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json_metrics.json

# With every display entry hidden, the line shows the metrics that vary,
# other than the hidden ones.  The format has no line-format, so it gets the
# default one that shows the sample.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json_metrics_auto.json

# A numeric field of a format whose metrics are named in the data, which is
# not one of the metrics and not in the line-format, can still be graphed.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":spectrogram duration" \
    ${test_dir}/logfile_json_metrics_duration.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_time, metric, value, kind, change FROM all_metrics ORDER BY log_time, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json_metrics.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT format, name, roles FROM lnav_format_values WHERE format LIKE 'test_json_metrics%' ORDER BY format, name" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json_metrics.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT * FROM lnav_format_metrics WHERE format LIKE 'test_json_metrics%'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json_metrics.json

# Hiding an entry, showing a hidden one, and showing a metric the format does
# not list are reflected in the line and in lnav_format_metrics.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':hide-fields test_json_metrics_log.cpu' \
    -c ':show-fields test_json_metrics_log.disk.pct' \
    -c ':show-fields test_json_metrics_log.queue.depth' \
    ${test_dir}/logfile_json_metrics.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':hide-fields test_json_metrics_log.cpu' \
    -c ':show-fields test_json_metrics_log.disk.pct' \
    -c ':show-fields test_json_metrics_log.queue.depth' \
    -c ";SELECT name, short_name, hidden, shown FROM lnav_format_metrics WHERE format = 'test_json_metrics_log'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json_metrics.json

# Display entries with a name-pattern give the metrics they match a unit and
# range without being columns, and a hidden one keeps them off the line.  A
# setting an entry with a name leaves out, like gc.pause_ms's divisor, comes
# from a pattern.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_metric_units.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':show-fields test_metric_units_log.gc.pause_ms' \
    -c ':show-fields test_metric_units_log.debug.queue' \
    ${test_dir}/logfile_metric_units.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':goto 1' \
    -c ";UPDATE lnav_views SET options = json_object('row-details', 'show') WHERE name = 'log'" \
    ${test_dir}/logfile_metric_units.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT name, name_pattern, min, max, unit_suffix, unit_divisor, hidden, shown FROM lnav_format_metrics WHERE format = 'test_metric_units_log'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metric_units.json

# all_metrics has the unit each sample is shown with, after the patterns
# fill in what an entry with a name leaves out.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT DISTINCT metric, unit_suffix, unit_divisor FROM all_metrics ORDER BY metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metric_units.json

# lnav_file_metrics has the same unit for each metric, without reading the
# samples, and the format's range, from the patterns too, not divided.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT metric, unit_suffix, unit_divisor, range_min, range_max FROM lnav_file_metrics ORDER BY metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metric_units.json

# Errors in the metric section of a format.
run_cap_test ${lnav_test} -n \
    -I ${test_dir}/bad-config-metric \
    ${test_dir}/logfile_json_metrics.json

# all_metrics narrows its scan by log_time and metric constraints; the rows
# have to be the same as without them.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_time, metric, value, change FROM all_metrics WHERE metric IN ('requests', 'mem.used{host=b}') AND log_time >= '2026-11-17T06:00:10' ORDER BY log_time, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json_metrics.json

# A time spelled with a space sorts differently as text, so it can't narrow
# the scan.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_time, source, metric, value FROM all_metrics WHERE metric = 'cpu_pct' AND log_time >= '2026-04-14 10:00:10' AND log_time < '2026-04-14T10:00:20' ORDER BY log_time, source" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# lnav_file_metrics lists each file's metrics from what was kept while
# indexing: CSV columns, and metrics named in the data, with counters as
# their changes.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT source, metric, kind, count, min, max, round(mean, 2) AS mean, p50, earliest, latest, latest_usecs - earliest_usecs AS span_us FROM lnav_file_metrics ORDER BY source, metric" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json_metrics.json \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# A log_time_usecs range narrows the scan like a log_time one, and gives the
# same rows.
run_cap_test ${lnav_test} -n \
    -c ";SELECT a.log_time, a.metric, a.value FROM all_metrics AS a, (SELECT min(log_time_usecs) AS t0 FROM all_metrics) AS f WHERE a.metric = 'cpu_pct' AND a.log_time_usecs >= f.t0 + 10000000 AND a.log_time_usecs < f.t0 + 20000000 ORDER BY a.log_time, a.source" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_metrics.csv \
    ${test_dir}/logfile_metrics2.csv

# A display entry's series share a column when they were never sampled at
# the same time (cpu.pct after a restart with a new cc label), and get a
# column each, labeled with their labels, when they were (the two hosts of
# mem.used, and requests, whose new series starts at the old one's last
# sample).
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json_metrics_restart.json

# The overlay lists a row's metrics by name, ignoring case, with the series
# of a metric under it by their labels, whatever order the file has them in.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";UPDATE lnav_views SET options = json_object('row-details', 'show') WHERE name = 'log'" \
    ${test_dir}/logfile_json_metrics_order.json

# With other messages at the top of the LOG view, the nearest row of metrics
# above them is kept at the top, so the metrics for that time can be seen.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':goto 3' \
    ${test_dir}/logfile_json_metrics.json \
    ${test_dir}/logfile_metrics_interleaved.0

# A row of metrics at the top of the view shows them already, so nothing is
# kept above it.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':goto 5' \
    ${test_dir}/logfile_json_metrics.json \
    ${test_dir}/logfile_metrics_interleaved.0
