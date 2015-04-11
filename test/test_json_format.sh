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


run_test ${lnav_test} -n -d /tmp/lnav.err \
    -I ${test_dir} \
    -c ';select * from test_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json.json

check_output "log levels not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,user
0,p.0,2013-09-06 20:00:48.124,0,trace,0,<NULL>
1,p.0,2013-09-06 20:00:49.124,1000,info,0,<NULL>
2,p.0,2013-09-06 22:00:49.124,7200000,info,0,steve@example.com
4,p.0,2013-09-06 22:00:59.124,10000,debug5,0,<NULL>
5,p.0,2013-09-06 22:00:59.124,0,debug4,0,<NULL>
6,p.0,2013-09-06 22:00:59.124,0,debug3,0,<NULL>
7,p.0,2013-09-06 22:00:59.124,0,debug2,0,<NULL>
8,p.0,2013-09-06 22:00:59.124,0,debug,0,<NULL>
9,p.0,2013-09-06 22:01:49.124,50000,stats,0,<NULL>
10,p.0,2013-09-06 22:01:49.124,0,warning,0,<NULL>
11,p.0,2013-09-06 22:01:49.124,0,error,0,<NULL>
12,p.0,2013-09-06 22:01:49.124,0,critical,0,<NULL>
13,p.0,2013-09-06 22:01:49.124,0,fatal,0,<NULL>
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
