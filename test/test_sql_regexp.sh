#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -n \
   -c ";SELECT * FROM syslog_log, regexp_capture_into_json(log_body, '"'"'"(?<value>[^"'"'"]+)')" \
   -c ":write-csv-to -" \
   ${test_dir}/logfile_syslog.3
