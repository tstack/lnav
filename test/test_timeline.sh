#! /bin/bash

export YES_COLOR=1

export TZ=UTC

touch -t 200711030923 ${srcdir}/logfile_glog.0
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ":hide-fields log_opid" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = '1234' where log_line in (1, 3, 6)" \
    -c ";UPDATE all_opids SET description = 'test-1' WHERE opid = '1234'" \
    -c ";UPDATE all_logs set log_opid = '5678' where log_line = 2" \
    -c ";UPDATE all_opids SET description = 'test-2' WHERE opid = '5678'" \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = '1234' where log_line in (1, 3, 6)" \
    -c ";UPDATE all_opids SET description = 'test-1' WHERE opid = '1234'" \
    -c ";UPDATE all_logs set log_opid = '5678' where log_line = 2" \
    -c ";UPDATE all_opids SET description = 'test-2' WHERE opid = '5678'" \
    -c ";SELECT * FROM all_opids" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = '1234' WHERE log_line in (1, 3, 6)" \
    -c ";UPDATE all_logs set log_opid = NULL WHERE log_line = 2" \
    -c ";SELECT log_line,log_opid FROM all_logs" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = '1234' WHERE log_line in (1, 3, 6)" \
    -c ";UPDATE all_logs set log_opid = NULL WHERE log_line = 2" \
    -c ":switch-to-view timeline" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = '1234' WHERE log_line in (1, 3, 6)" \
    -c ";UPDATE all_logs set log_opid = NULL" \
    -c ":switch-to-view timeline" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_generic.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':filter-in CdysLK1XpcrXOpVDuh' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':filter-out CdysLK1XpcrXOpVDuh' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-file *' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':close *' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-lines-before 2011-11-03 00:19:37' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-lines-after 2011-11-03 00:20:30' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_strace_log.1

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_caddy_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_opid FROM all_logs" \
    ${test_dir}/logfile_caddy_log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_caddy_log.1

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_opid FROM all_logs" \
    ${test_dir}/logfile_caddy_log.1

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_opid FROM all_logs" \
    ${test_dir}/logfile_rust_tracing.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line, log_thread_id FROM all_logs" \
    ${test_dir}/logfile_rust_tracing.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view timeline" \
    ${test_dir}/logfile_rust_tracing.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view timeline" \
    -c ":next-mark file" \
    ${test_dir}/logfile_access_log.0 \
    ${test_dir}/logfile_access_log.1

run_cap_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ":partition-name middle" \
    -c ":goto 5" \
    -c ":partition-name end" \
    -c ':switch-to-view timeline' \
    -c ":goto 0" \
    ${test_dir}/logfile_glog.0

# hide opid rows in the timeline
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':hide-in-timeline opid' \
    ${test_dir}/logfile_glog.0

# hide multiple row types
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':hide-in-timeline logfile opid' \
    ${test_dir}/logfile_glog.0

# hide and then show
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':hide-in-timeline opid' \
    -c ':show-in-timeline opid' \
    ${test_dir}/logfile_glog.0

# error: unknown row type
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-in-timeline badtype' \
    ${test_dir}/logfile_glog.0

# error: no arguments
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-in-timeline' \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_mongodb.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_cloudflare.1

# timeline-metric: shorthand adds a sparkline row from all_metrics
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric cpu_pct' \
    ${test_dir}/logfile_glog.0 ${test_dir}/logfile_glog_metrics.csv

# timeline-metric: two simultaneous metrics
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric cpu_pct' \
    -c ':timeline-metric mem_mb' \
    ${test_dir}/logfile_glog.0 ${test_dir}/logfile_glog_metrics.csv

# clear-timeline-metric removes one of the sparklines
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric cpu_pct' \
    -c ':timeline-metric mem_mb' \
    -c ':clear-timeline-metric cpu_pct' \
    ${test_dir}/logfile_glog.0 ${test_dir}/logfile_glog_metrics.csv

# timeline-metric: bare metric name aggregates samples across every
# loaded metrics file that ships a matching column (day-split CSV
# case); qualified "stem.metric" picks just one file's column.
# cpu_pct lives in both fixtures (with interleaved timestamps);
# disk_iops only lives in metrics2.
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric cpu_pct' \
    -c ':timeline-metric disk_iops' \
    ${test_dir}/logfile_glog.0 \
    ${test_dir}/logfile_glog_metrics.csv \
    ${test_dir}/logfile_glog_metrics2.csv

# timeline-metric: qualified form scopes to one source
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric logfile_glog_metrics.cpu_pct' \
    -c ':timeline-metric logfile_glog_metrics2.cpu_pct' \
    ${test_dir}/logfile_glog.0 \
    ${test_dir}/logfile_glog_metrics.csv \
    ${test_dir}/logfile_glog_metrics2.csv

# timeline-metric-sql: user SQL is wrapped as a subquery
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric-sql cpu_label SELECT log_time, value FROM all_metrics WHERE metric = "cpu_pct"' \
    ${test_dir}/logfile_glog.0 ${test_dir}/logfile_glog_metrics.csv

# timeline-metric-sql: humanized text values ("500MB", "20ms") get
# parsed via humanize::try_from so the sparkline scales against the
# real base-unit range and the status-bar value picks up the unit.
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric-sql mem_label SELECT log_time, raw_value AS value FROM all_metrics WHERE metric = "mem"' \
    -c ':timeline-metric-sql lat_label SELECT log_time, raw_value AS value FROM all_metrics WHERE metric = "latency"' \
    ${test_dir}/logfile_glog.0 ${test_dir}/logfile_glog_metrics_units.csv

# error: clear-timeline-metric on an unknown label
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':clear-timeline-metric nope' \
    ${test_dir}/logfile_glog.0

# error: timeline-metric-sql with a syntactically-invalid query
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric-sql oops SELECT bogus FROM nowhere' \
    ${test_dir}/logfile_glog.0

# error: timeline-metric-sql missing the log_time column
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric-sql oops SELECT 1 AS value FROM all_logs' \
    ${test_dir}/logfile_glog.0

# error: timeline-metric-sql missing the value column
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric-sql oops SELECT log_time FROM all_logs' \
    ${test_dir}/logfile_glog.0

# error: timeline-metric-sql missing both required columns
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric-sql oops SELECT 1, 2 FROM all_logs' \
    ${test_dir}/logfile_glog.0

# error: timeline-metric-sql with a whitespace-containing label
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c $':timeline-metric-sql "bad\tlabel" SELECT log_time, 1 AS value FROM all_logs' \
    ${test_dir}/logfile_glog.0

# timeline-metric: an unknown metric name renders an inline error on
# the sparkline row instead of failing the command — the data may
# just not have been loaded yet.
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric nonexistent_metric' \
    ${test_dir}/logfile_glog.0 ${test_dir}/logfile_glog_metrics.csv

# timeline-metric-sql: a query that validates at command time but
# fails at collection time (here via raise_error) shows the sqlite
# error inline on the sparkline row.
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    -c ':timeline-metric-sql oops SELECT log_time, raise_error("metric source unavailable") AS value FROM all_metrics' \
    ${test_dir}/logfile_glog.0 ${test_dir}/logfile_glog_metrics.csv
