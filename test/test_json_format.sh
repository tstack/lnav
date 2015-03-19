#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json.json

check_output "json log format is not working" <<EOF
2013-09-06T20:00:49.124 INFO Starting up service
2013-09-06T22:00:49.124 INFO Shutting down service
  user: steve@example.com
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 1" \
    -c ":pipe-line-to sed -e 's/2013//g'" \
    -c ":switch-to-view text" \
    ${test_dir}/logfile_json.json
check_output "pipe-line-to is not working" <<EOF
-09-06T22:00:49.124 INFO Shutting down service
  user: steve@example.com

EOF
