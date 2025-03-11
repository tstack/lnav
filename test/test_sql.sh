#! /bin/bash

export TZ=UTC
export YES_COLOR=1

lnav_test="${top_builddir}/src/lnav-test"
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -nN \
    -c ";SELECT 1 = ?"

# XXX The timestamp on the file is used to determine the year for syslog files.
touch -t 200711030923 ${test_dir}/logfile_syslog.0
run_cap_test ${lnav_test} -n \
    -c ";.dump syslog_log.sql syslog_log" \
    ${test_dir}/logfile_syslog.0

run_cap_test cat syslog_log.sql

run_cap_test ${lnav_test} -n \
    -c ";.read nonexistent-file" \
    ${test_dir}/logfile_empty.0

run_test ${lnav_test} -n \
    -c ";.read ${test_dir}/file_for_dot_read.sql" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output ".read did not work?" <<EOF
log_line,log_body
1, attempting to mount entry /auto/opt
EOF


run_cap_test ${lnav_test} -n \
    -c ";SELECT replicate('foobar', 120)" \
    ${test_dir}/logfile_empty.0

cp ${srcdir}/logfile_syslog.2 logfile_syslog_test.2
touch -t 201511030923 logfile_syslog_test.2
run_cap_test ${lnav_test} -n \
    -c ";SELECT *, log_msg_schema FROM all_logs" \
    -c ":write-csv-to -" \
    logfile_syslog_test.2

run_cap_test ${lnav_test} -n \
    -c ";SELECT fields FROM logfmt_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_logfmt.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT sc_substatus FROM w3c_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_w3c.3

run_cap_test ${lnav_test} -n \
    -c ";SELECT cs_headers FROM w3c_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_w3c.3

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM generate_series()" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM generate_series(1)" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT 1 AS inum, NULL AS nul, 2.0 AS fnum, 'abc' AS str" \
    -c ";SELECT \$inum, \$nul, \$fnum, \$str, raise_error('oops!')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT raise_error('oops!')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT basename(filepath) as name, content, length(content) FROM lnav_file" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_empty.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT distinct xp.node_text FROM lnav_file, xpath('//author', content) as xp" \
    -c ":write-csv-to -" \
    ${test_dir}/books.xml

gzip -c ${srcdir}/logfile_json.json > logfile_json.json.gz
dd if=logfile_json.json.gz of=logfile_json-trunc.json.gz bs=64 count=2

# TODO re-enable this
#run_test ${lnav_test} -n \
#    -c ";SELECT content FROM lnav_file" \
#    logfile_json-trunc.json.gz

#check_error_output "invalid gzip file working?" <<EOF
#command-option:1: error: unable to uncompress: logfile_json-trunc.json.gz -- buffer error
#EOF

run_test ${lnav_test} -n \
    -c ";SELECT jget(rc.content, '/ts') AS ts FROM lnav_file, regexp_capture(lnav_file.content, '.*\n') as rc" \
    -c ":write-csv-to -" \
    logfile_json.json.gz

check_output "jget on file content not working?" <<EOF
ts
2013-09-06T20:00:48.124817Z
2013-09-06T20:00:49.124817Z
2013-09-06T22:00:49.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06 22:01:00Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
EOF

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_file SET filepath='foo' WHERE endswith(filepath, '_log.0')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c "|rename-stdin" \
    ${test_dir}/logfile_access_log.0 < /dev/null

run_cap_test ${lnav_test} -n \
    -c "|rename-stdin foo" \
    ${test_dir}/logfile_access_log.0 < /dev/null

run_cap_test ${lnav_test} -n \
    -c "|rename-stdin foo" \
    -c ";SELECT filepath FROM lnav_file" <<EOF
Hello, World!
EOF

run_test ${lnav_test} -n \
    -c ";SELECT basename(filepath),format,lines,time_offset FROM lnav_file LIMIT 2" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0 \
    ${test_dir}/logfile_access_log.1

check_output "lnav_file table is not working?" <<EOF
basename(filepath),format,lines,time_offset
logfile_access_log.0,access_log,3,0
logfile_access_log.1,access_log,1,0
EOF

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_file SET time_offset = 60 * 1000" \
    ${test_dir}/logfile_access_log.0 \
    ${test_dir}/logfile_access_log.1

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_file SET time_offset=14400000 WHERE endswith(filepath, 'logfile_block.1')" \
    ${test_dir}/logfile_block.1 \
    ${test_dir}/logfile_block.2

check_output "time_offset in lnav_file table is not reordering?" <<EOF
Wed May 19 12:00:01  2021 line 1
/abc/def
Wed May 19 12:00:02 +0000 2021 line 2
Wed May 19 12:00:03  2021 line 3
/ghi/jkl
Wed May 19 12:00:04 +0000 2021 line 4
EOF


run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM access_log LIMIT 0" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c "|${test_dir}/empty-result.lnav" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_line(), log_msg_line()" \
    ${test_dir}/logfile_uwsgi.0

run_cap_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_line(), log_msg_line()" \
    ${test_dir}/logfile_empty.0

run_cap_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ";SELECT log_top_line(), log_msg_line()" \
    ${test_dir}/logfile_multiline.0

run_cap_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_datetime()" \
    ${test_dir}/logfile_uwsgi.0

run_cap_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_datetime()" \
    ${test_dir}/logfile_empty.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM uwsgi_log LIMIT 1" \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_uwsgi.0

run_test ${lnav_test} -n \
    -c ";SELECT s_runtime FROM uwsgi_log LIMIT 5" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_uwsgi.0

check_output "uwsgi scaling not working?" <<EOF
s_runtime
0.129
0.035
6.8e-05
0.016
0.01
EOF

run_cap_test env TZ=UTC ${lnav_test} -n \
    -c ";SELECT bro_conn_log.bro_duration as duration, bro_conn_log.bro_uid, group_concat( distinct (bro_method || ' ' || bro_host)) as req from bro_http_log, bro_conn_log where bro_http_log.bro_uid = bro_conn_log.bro_uid group by bro_http_log.bro_uid order by duration desc limit 10" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_bro_http.log.0 ${test_dir}/logfile_bro_conn.log.0

run_cap_test env TZ=UTC ${lnav_test} -n \
    -c ";SELECT * FROM bro_http_log LIMIT 5" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test env TZ=UTC ${lnav_test} -n \
    -c ";SELECT * FROM bro_http_log WHERE log_level = 'error'" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_bro_http.log.0

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line > 100000' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
EOF

run_cap_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line > -100000' \
    ${test_dir}/logfile_access_log.0

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line < -10000' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
EOF

run_cap_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line > -10000' \
    ${test_dir}/logfile_access_log.0

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line < 0' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
EOF

run_cap_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line <= 0' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line >= 0' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ';select sc_bytes from access_log' \
    -c ':spectrogram sc_bytes' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ';select log_time,sc_bytes from access_log' \
    -c ':spectrogram sc_byes' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ';select log_time,c_ip from access_log' \
    -c ':spectrogram c_ip' \
    ${test_dir}/logfile_access_log.0

run_cap_test env TZ=UTC LC_ALL=C ${lnav_test} -n \
    -c ':spectrogram bro_response_body_len' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ';select log_time,sc_bytes from access_log order by log_time desc' \
    -c ':spectrogram sc_bytes' \
    ${test_dir}/logfile_access_log.0

cp ${srcdir}/logfile_syslog_with_mixed_times.0 logfile_syslog_with_mixed_times_test.0
touch -t 201511030923 logfile_syslog_with_mixed_times_test.0
run_test ${lnav_test} -n \
    -c ";select log_time,log_actual_time from syslog_log" \
    -c ':write-csv-to -' \
    logfile_syslog_with_mixed_times_test.0

check_output "log_actual_time column not working" <<EOF
log_time,log_actual_time
2015-09-13 00:58:45.000,2015-09-13 00:58:45.000
2015-09-13 00:59:30.000,2015-09-13 00:59:30.000
2015-09-13 01:23:54.000,2015-09-13 01:23:54.000
2015-09-13 03:12:04.000,2015-09-13 03:12:04.000
2015-09-13 03:12:04.000,2015-09-13 03:12:04.000
2015-09-13 03:12:04.000,2015-09-13 01:25:39.000
2015-09-13 03:12:04.000,2015-09-13 03:12:04.000
2015-09-13 03:12:58.000,2015-09-13 03:12:58.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
2015-09-13 03:46:03.000,2015-09-13 03:13:16.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
EOF


run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ';select log_line, log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part
0,<NULL>
1,middle
2,middle
EOF

run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ";update access_log set log_part = NULL where log_line = 1" \
    -c ';select log_line, log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part
0,<NULL>
1,<NULL>
2,<NULL>
EOF

run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ";update access_log set log_part = NULL where log_line = 2" \
    -c ';select log_line, log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part
0,<NULL>
1,middle
2,middle
EOF


run_cap_test ${lnav_test} -n \
    -I "${top_srcdir}/test" \
    -c ";select * from web_status" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";select * from access_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";select * from access_log where log_level >= 'warning'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";select * from syslog_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

run_test ${lnav_test} -n \
    -c ";select * from syslog_log where log_time >= NULL" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "log_time collation failed on null" <<EOF
log_line,log_time,log_level,log_hostname,log_msgid,log_pid,log_pri,log_procname,log_struct,log_syslog_tag,syslog_version,log_part,log_idle_msecs,log_mark,log_comment,log_tags,log_annotations,log_filters
EOF


run_test ${lnav_test} -n \
    -c ";select log_line from syslog_log where log_time >= datetime('2007-11-03T09:47:02.000')" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "log_time collation is wrong" <<EOF
log_line
3
EOF


run_cap_test ${lnav_test} -n \
    -c ':filter-in sudo' \
    -c ";select * from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

run_test ${lnav_test} -n \
    -c ':goto 1' \
    -c ";select log_line, log_pid, col_0 from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.1

check_output "logline table is not working" <<EOF
log_line,log_pid,col_0
1,16442,/auto/opt
EOF

run_test ${lnav_test} -n \
    -c ";select sc_bytes from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "logline table is not working for defined columns" <<EOF
sc_bytes
134
46210
78929
EOF


run_test ${lnav_test} -n \
    -c ':goto 1' \
    -c ":summarize col_0" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.1

check_output "summarize is not working" <<EOF
c_col_0,count_col_0
/auto/opt,1
EOF


run_cap_test ${lnav_test} -n \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ':write-to -' \
    ${test_dir}/logfile_access_log.0

export SQL_ENV_VALUE="foo bar,baz"

run_test ${lnav_test} -n \
    -c ';select $SQL_ENV_VALUE as val' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "env vars are not working in SQL" <<EOF
val
"foo bar,baz"
EOF


run_test ${lnav_test} -n \
    -c ";SELECT name,value FROM environ WHERE name = 'SQL_ENV_VALUE'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "environ table is not working in SQL" <<EOF
name,value
SQL_ENV_VALUE,"foo bar,baz"
EOF


run_cap_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name) VALUES (null)' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES (null, null)' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO environ (name, value) VALUES ('', null)" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO environ (name, value) VALUES ('foo=bar', 'bar')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO environ (name, value) VALUES ('SQL_ENV_VALUE', 'bar')" \
    ${test_dir}/logfile_access_log.0

run_test ${lnav_test} -n \
    -c ";INSERT OR IGNORE INTO environ (name, value) VALUES ('SQL_ENV_VALUE', 'bar')" \
    -c ";SELECT * FROM environ WHERE name = 'SQL_ENV_VALUE'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "insert into environ table works" <<EOF
name,value
SQL_ENV_VALUE,"foo bar,baz"
EOF


run_test ${lnav_test} -n \
    -c ";REPLACE INTO environ (name, value) VALUES ('SQL_ENV_VALUE', 'bar')" \
    -c ";SELECT * FROM environ WHERE name = 'SQL_ENV_VALUE'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "replace into environ table works" <<EOF
name,value
SQL_ENV_VALUE,bar
EOF


run_test ${lnav_test} -n \
    -c ";INSERT INTO environ (name, value) VALUES ('foo_env', 'bar')" \
    -c ';SELECT $foo_env as val' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "insert into environ table does not work" <<EOF
val
bar
EOF


run_test ${lnav_test} -n \
    -c ";UPDATE environ SET name='NEW_ENV_VALUE' WHERE name='SQL_ENV_VALUE'" \
    -c ";SELECT * FROM environ WHERE name like '%ENV_VALUE'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "update environ table does not work" <<EOF
name,value
NEW_ENV_VALUE,"foo bar,baz"
EOF


run_test ${lnav_test} -n \
    -c ";DELETE FROM environ WHERE name='SQL_ENV_VALUE'" \
    -c ";SELECT * FROM environ WHERE name like '%ENV_VALUE'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "delete from environ table does not work" <<EOF
name,value
EOF


run_test ${lnav_test} -n \
    -c ';DELETE FROM environ' \
    -c ';SELECT * FROM environ' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "delete environ table does not work" <<EOF
name,value
EOF



schema_dump() {
    ${lnav_test} -n -c ';.schema' ${test_dir}/logfile_access_log.0 | head -n21
}

run_test schema_dump

check_output "schema view is not working" <<EOF
ATTACH DATABASE '' AS 'main';
CREATE VIRTUAL TABLE environ USING environ_vtab_impl();
CREATE VIRTUAL TABLE lnav_static_files USING lnav_static_file_vtab_impl();
CREATE VIRTUAL TABLE lnav_view_filter_stats USING lnav_view_filter_stats_impl();
CREATE VIRTUAL TABLE lnav_views USING lnav_views_impl();
CREATE VIRTUAL TABLE lnav_view_files USING lnav_view_files_impl();
CREATE VIRTUAL TABLE lnav_view_stack USING lnav_view_stack_impl();
CREATE VIRTUAL TABLE lnav_view_filters USING lnav_view_filters_impl();
CREATE VIRTUAL TABLE lnav_file USING lnav_file_impl();
CREATE VIRTUAL TABLE lnav_file_metadata USING lnav_file_metadata_impl();
CREATE VIEW lnav_view_filters_and_stats AS
  SELECT * FROM lnav_view_filters LEFT NATURAL JOIN lnav_view_filter_stats;
CREATE VIRTUAL TABLE regexp_capture USING regexp_capture_impl();
CREATE VIRTUAL TABLE regexp_capture_into_json USING regexp_capture_into_json_impl();
CREATE VIRTUAL TABLE xpath USING xpath_impl();
CREATE VIRTUAL TABLE fstat USING fstat_impl();
CREATE TABLE [1m[35mlnav_events[0m (
   ts TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%f', 'now')),
   content TEXT
);
CREATE TABLE [1m[35mhttp_status_codes[0m
EOF


run_cap_test ${lnav_test} -n \
    -c ";select * from nonexistent_table" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";delete from access_log" \
    ${test_dir}/logfile_access_log.0

touch -t 201504070732 ${test_dir}/logfile_pretty.0
run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":goto 21" \
    -c ":partition-name end" \
    -c ";select log_line,log_part,log_time from syslog_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_pretty.0

check_output "partition-name does not work" <<EOF
log_line,log_part,log_time
0,<NULL>,2015-04-07 00:49:42.000
1,middle,2015-04-07 05:49:53.000
18,middle,2015-04-07 07:31:56.000
20,middle,2015-04-07 07:31:56.000
21,end,2015-04-07 07:31:56.000
22,end,2015-04-07 07:32:56.000
EOF

run_cap_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":goto 21" \
    -c ":partition-name end" \
    -c ":goto 0" \
    -c ":next-section" \
    -c ";SELECT log_top_line()" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ":next-section" \
    -c ";SELECT log_top_line()" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_pretty.0

run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":clear-partition" \
    -c ";select log_line, log_part from access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "clear-partition does not work" <<EOF
log_line,log_part
0,<NULL>
1,<NULL>
2,<NULL>
EOF

run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":goto 2" \
    -c ":clear-partition" \
    -c ";select log_line, log_part from access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "clear-partition does not work when in the middle of a part" <<EOF
log_line,log_part
0,<NULL>
1,<NULL>
2,<NULL>
EOF

# test the partitions defined in the format
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";SELECT log_line, log_part, log_body FROM syslog_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_partitions.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM openam_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_openam.0

touch -t 200711030000 ${srcdir}/logfile_for_join.0

run_cap_test ${lnav_test} -d "/tmp/lnav.err" -n \
    -c ";select log_line, col_0 from logline" \
    ${test_dir}/logfile_for_join.0

run_cap_test ${lnav_test} -d "/tmp/lnav.err" -n \
    -c ";select col_0 from logline where log_line > 4" \
    ${test_dir}/logfile_for_join.0

run_test ${lnav_test} -d "/tmp/lnav.err" -n \
    -c ":goto 1" \
    -c ":create-logline-table join_group" \
    -c ":goto 2" \
    -c ";select logline.log_line as llline, join_group.log_line as jgline from logline, join_group where logline.col_0 = join_group.col_2" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_for_join.0

check_output "create-logline-table is not working" <<EOF
llline,jgline
2,1
2,8
9,1
9,8
EOF


run_cap_test ${lnav_test} -n \
    -c ";select log_body from syslog_log where log_procname = 'automount'" \
    < ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ";select log_body from syslog_log where log_procname = 'sudo'" \
    < ${test_dir}/logfile_syslog.0

# Create a dummy database for the next couple of tests to consume.
touch empty
rm simple-db.db
run_test ${lnav_test} -n \
    -c ";ATTACH DATABASE 'simple-db.db' as 'db'" \
    -c ";CREATE TABLE IF NOT EXISTS db.person ( id integer PRIMARY KEY, first_name text, last_name, age integer )" \
    -c ";INSERT INTO db.person(id, first_name, last_name, age) VALUES (0, 'Phil', 'Myman', 30)" \
    -c ";INSERT INTO db.person(id, first_name, last_name, age) VALUES (1, 'Lem', 'Hewitt', 35)" \
    -c ";DETACH DATABASE 'db'" \
    empty

check_output "Could not create db?" <<EOF
EOF

# Test to see if lnav can recognize a sqlite3 db file passed in as an argument.
run_cap_test ${lnav_test} -n -c ";select * from person order by age asc" \
    simple-db.db

# Test to see if lnav can recognize a sqlite3 db file passed in as an argument.
# XXX: Need to pass in a file, otherwise lnav keeps trying to open syslog
# and we might not have sufficient privileges on the system the tests are being
# run on.
run_cap_test ${lnav_test} -n \
    -c ";attach database 'simple-db.db' as 'db'" \
    -c ';select * from person order by age asc' \
    empty

# Test to see if we can attach a database in LNAVSECURE mode.
export LNAVSECURE=1

run_cap_test ${lnav_test} -n \
    -c ";attach database 'simple-db.db' as 'db'" \
    empty

run_cap_test ${lnav_test} -n \
    -c ";attach database ':memdb:' as 'db'" \
    empty

run_cap_test ${lnav_test} -n \
    -c ";attach database '/tmp/memdb' as 'db'" \
    empty

run_cap_test ${lnav_test} -n \
    -c ";attach database 'file:memdb?cache=shared' as 'db'" \
    empty

unset LNAVSECURE


touch -t 201503240923 ${test_dir}/logfile_syslog_with_access_log.0
run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ";select * from access_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog_with_access_log.0

run_test ${lnav_test} -n \
    -c ";select log_text from generic_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_multiline.0

check_output "multiline data is not right?" <<EOF
[
    {
        "log_text": "2009-07-20 22:59:27,672:DEBUG:Hello, World!\n  How are you today?"
    },
    {
        "log_text": "2009-07-20 22:59:30,221:ERROR:Goodbye, World!"
    }
]
EOF

run_test ${lnav_test} -n \
    -c ";select log_text from generic_log where log_line = 1" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_multiline.0

check_output "able to select a continued line?" <<EOF
[

]
EOF


run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 (\w+), world!" \
    -c ";select col_0 from search_test1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "create-search-table is not working?" <<EOF
col_0
Hello
Goodbye
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 (\w+), World!" \
    -c ";select col_0 from search_test1 where log_line > 0" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "create-search-table is not working with where clause?" <<EOF
col_0
Goodbye
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 (?<word>\w+), World!" \
    -c ";select word, typeof(word) from search_test1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "create-search-table is not working?" <<EOF
word,typeof(word)
Hello,text
Goodbye,text
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 eth(?<ethnum>\d+)" \
    -c ";select typeof(ethnum) from search_test1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_syslog.2

check_output "regex type guessing is not working?" <<EOF
typeof(ethnum)
integer
integer
integer
EOF

run_cap_test ${lnav_test} -n \
    -c ":delete-search-table search_test1" \
    ${test_dir}/logfile_multiline.0

run_cap_test ${lnav_test} -n \
    -c ":create-logline-table search_test1" \
    -c ":delete-search-table search_test1" \
    ${test_dir}/logfile_multiline.0

run_cap_test ${lnav_test} -n \
    -c ":create-search-table search_test1 bad(" \
    ${test_dir}/logfile_multiline.0

NULL_GRAPH_SELECT_1=$(cat <<EOF
;SELECT value FROM (
              SELECT 10 as value
    UNION ALL SELECT null as value)
EOF
)

run_test ${lnav_test} -n \
    -c "$NULL_GRAPH_SELECT_1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "number column with null does not work?" <<EOF
value
10
<NULL>
EOF

run_test ${lnav_test} -n \
    -c ";SELECT regexp_capture.content FROM access_log, regexp_capture(access_log.cs_version, 'HTTP/(\d+\.\d+)') WHERE regexp_capture.capture_index = 1" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "joining log table with regexp_capture is not working?" <<EOF
content
1.0
1.0
1.0
EOF

run_cap_test ${lnav_test} -n \
    -c ';SELECT echoln(sc_bytes), 123 FROM access_log' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ';SELECT lnav_top_file()' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view db' \
    -c ';SELECT lnav_top_file()' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -Nn \
     -c ";select *,case match_index when 2 then replicate('abc', 1000) else '' end from regexp_capture_into_json('10;50;50;50;', '(\d+);')"

run_cap_test ${lnav_test} -n \
     -c ";.msgformats" \
     ${test_dir}/logfile_for_join.0

run_cap_test ${lnav_test} -Nn \
    -c ";SELECT log_line, sc_bytes, json_object('columns', json_object('sc_bytes', json_object('color', '#f00'))) AS __lnav_style__ FROM access_log"  \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -Nn \
    -c ";SELECT log_line, sc_bytes, json_object('columns', json_object('sc_ytes', json_object('color', '#f00'))) AS __lnav_style__ FROM access_log"  \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT bro_response_body_len AS bro_response_body_len1, humanize_file_size(bro_response_body_len) AS bro_response_body_len2 FROM bro_http_log ORDER BY bro_response_body_len DESC" \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -nN \
    -c ";SELECT '1us' AS Duration UNION ALL SELECT '500ms' UNION ALL SELECT '1s'"

run_cap_test env TEST_COMMENT="alignment demo" ${lnav_test} -nN -f- <<'EOF'
;SELECT 'left' AS "Alignment Demo", '{"columns": {"Alignment Demo": {"text-align": "start"}}}' AS __lnav_style__
 UNION ALL
 SELECT 'center', '{"columns": {"Alignment Demo": {"text-align": "center"}}}' AS __lnav_style__
 UNION ALL
 SELECT 'right', '{"columns": {"Alignment Demo": {"text-align": "end"}}}' AS __lnav_style__
:switch-to-view db
EOF

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM access_log" \
    -c ":hide-fields log_time" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM access_log" \
    -c ":hide-fields bad" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line FROM vmw_log WHERE log_opid = '7e1280cf'" \
    ${test_dir}/logfile_vpxd.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line_link FROM access_log WHERE log_line = 1" \
    -c ';SELECT log_line FROM access_log WHERE log_line_link = $log_line_link' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line_link FROM access_log WHERE log_line = 1" \
    -c ';SELECT log_line FROM access_log WHERE log_line_link > $log_line_link' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line_link FROM access_log WHERE log_line = 1" \
    -c ':switch-to-view log' \
    -c ':eval :goto $log_line_link' \
    ${test_dir}/logfile_access_log.0
