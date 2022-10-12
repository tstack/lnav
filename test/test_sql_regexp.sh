#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -n \
   -c ";SELECT * FROM syslog_log, regexp_capture_into_json(log_body, '"'"'"(?<value>[^"'"'"]+)')" \
   -c ":write-csv-to -" \
   ${test_dir}/logfile_syslog.3

run_cap_test ${lnav_test} -n \
   -c ";SELECT * from regexp_capture_into_json('foo=0x123e;', '(?<key>\w+)=(?<value>[^;]+)')" \
   ${test_dir}/logfile_syslog.3

run_cap_test ${lnav_test} -n \
   -c ";SELECT * from regexp_capture_into_json('foo=0x123e;', '(?<key>\w+)=(?<value>[^;]+)', json_object('convert-numbers', json('false')))" \
   ${test_dir}/logfile_syslog.3

run_cap_test ${lnav_test} -n \
   -c ";SELECT * from regexp_capture_into_json('foo=0x123e;', '(?<key>\w+)=(?<value>[^;]+)', '{abc')" \
   ${test_dir}/logfile_syslog.3

run_cap_test ${lnav_test} -n \
   -c ";SELECT * from regexp_capture_into_json('foo=123e;', '(?<key>\w+)=(?<value>[^;]+)')" \
   ${test_dir}/logfile_syslog.3

run_cap_test ${lnav_test} -nN \
   -c ";SELECT * from regexp_capture('abc=def;ghi=jkl;', '^(\w+)=([^;]+);')"

run_cap_test ${lnav_test} -nN \
   -c ";SELECT * from regexp_capture_into_json('abc=def;ghi=jkl;', '^(\w+)=([^;]+);')"
