#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json.json

check_output "json log format is not working" <<EOF
2013-09-06T20:00:48.124 TRACE trace test
2013-09-06T20:00:49.124 INFO Starting up service
2013-09-06T22:00:49.124 INFO Shutting down service
  user: steve@example.com
2013-09-06T22:00:59.124 DEBUG5 Details...
2013-09-06T22:00:59.124 DEBUG4 Details...
2013-09-06T22:00:59.124 DEBUG3 Details...
2013-09-06T22:00:59.124 DEBUG2 Details...
2013-09-06T22:00:59.124 DEBUG Details...
2013-09-06T22:01:49.124 STATS 1 beat per second
2013-09-06T22:01:49.124 WARNING not looking good
2013-09-06T22:01:49.124 ERROR looking bad
2013-09-06T22:01:49.124 CRITICAL sooo bad
2013-09-06T22:01:49.124 FATAL shoot
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from test_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json.json

check_output "log levels not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,user
0,<NULL>,2013-09-06 20:00:48.124,0,trace,0,<NULL>
1,<NULL>,2013-09-06 20:00:49.124,1000,info,0,<NULL>
2,<NULL>,2013-09-06 22:00:49.124,7200000,info,0,steve@example.com
4,<NULL>,2013-09-06 22:00:59.124,10000,debug5,0,<NULL>
5,<NULL>,2013-09-06 22:00:59.124,0,debug4,0,<NULL>
6,<NULL>,2013-09-06 22:00:59.124,0,debug3,0,<NULL>
7,<NULL>,2013-09-06 22:00:59.124,0,debug2,0,<NULL>
8,<NULL>,2013-09-06 22:00:59.124,0,debug,0,<NULL>
9,<NULL>,2013-09-06 22:01:49.124,50000,stats,0,<NULL>
10,<NULL>,2013-09-06 22:01:49.124,0,warning,0,<NULL>
11,<NULL>,2013-09-06 22:01:49.124,0,error,0,<NULL>
12,<NULL>,2013-09-06 22:01:49.124,0,critical,0,<NULL>
13,<NULL>,2013-09-06 22:01:49.124,0,fatal,0,<NULL>
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 2" \
    -c ":pipe-line-to sed -e 's/2013//g'" \
    -c ":switch-to-view text" \
    ${test_dir}/logfile_json.json
check_output "pipe-line-to is not working" <<EOF
-09-06T22:00:49.124 INFO Shutting down service
  user: steve@example.com

EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_nested_json.json

check_output "json log format is not working" <<EOF
2013-09-06T20:00:48.124 TRACE trace test
  @fields: { "lvl": "TRACE", "msg": "trace test"}
2013-09-06T20:00:49.124 INFO Starting up service
  @fields: { "lvl": "INFO", "msg": "Starting up service"}
2013-09-06T22:00:49.124 INFO Shutting down service
  @fields/user: steve@example.com
  @fields: { "lvl": "INFO", "msg": "Shutting down service", "user": "steve@example.com"}
2013-09-06T22:00:59.124 DEBUG5 Details...
  @fields: { "lvl": "DEBUG5", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG4 Details...
  @fields: { "lvl": "DEBUG4", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG3 Details...
  @fields: { "lvl": "DEBUG3", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG2 Details...
  @fields: { "lvl": "DEBUG2", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG Details...
  @fields: { "lvl": "DEBUG", "msg": "Details..."}
2013-09-06T22:01:49.124 STATS 1 beat per second
  @fields: { "lvl": "STATS", "msg": "1 beat per second"}
2013-09-06T22:01:49.124 WARNING not looking good
  @fields: { "lvl": "WARNING", "msg": "not looking good"}
2013-09-06T22:01:49.124 ERROR looking bad
  @fields: { "lvl": "ERROR", "msg": "looking bad"}
2013-09-06T22:01:49.124 CRITICAL sooo bad
  @fields: { "lvl": "CRITICAL", "msg": "sooo bad"}
2013-09-06T22:01:49.124 FATAL shoot
  @fields: { "lvl": "FATAL", "msg": "shoot"}
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from ntest_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_nested_json.json

check_output "log levels not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,@fields/user
0,<NULL>,2013-09-06 20:00:48.124,0,trace,0,<NULL>
2,<NULL>,2013-09-06 20:00:49.124,1000,info,0,<NULL>
4,<NULL>,2013-09-06 22:00:49.124,7200000,info,0,steve@example.com
7,<NULL>,2013-09-06 22:00:59.124,10000,debug5,0,<NULL>
9,<NULL>,2013-09-06 22:00:59.124,0,debug4,0,<NULL>
11,<NULL>,2013-09-06 22:00:59.124,0,debug3,0,<NULL>
13,<NULL>,2013-09-06 22:00:59.124,0,debug2,0,<NULL>
15,<NULL>,2013-09-06 22:00:59.124,0,debug,0,<NULL>
17,<NULL>,2013-09-06 22:01:49.124,50000,stats,0,<NULL>
19,<NULL>,2013-09-06 22:01:49.124,0,warning,0,<NULL>
21,<NULL>,2013-09-06 22:01:49.124,0,error,0,<NULL>
23,<NULL>,2013-09-06 22:01:49.124,0,critical,0,<NULL>
25,<NULL>,2013-09-06 22:01:49.124,0,fatal,0,<NULL>
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 4" \
    -c ":pipe-line-to sed -e 's/2013//g'" \
    -c ":switch-to-view text" \
    ${test_dir}/logfile_nested_json.json
check_output "pipe-line-to is not working" <<EOF
-09-06T22:00:49.124 INFO Shutting down service
  @fields/user: steve@example.com
  @fields: { "lvl": "INFO", "msg": "Shutting down service", "user": "steve@example.com"}

EOF
