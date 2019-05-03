#!/usr/bin/env bash

run_test ${lnav_test} -n \
    -c ":config /ui/theme-defs/default/styles/text/color #f" \
    ${test_dir}/logfile_access_log.0

check_error_output "config bad color" <<EOF
error:command-option:1:Could not parse color: #f
EOF

run_test ${lnav_test} -n \
    -c ":config /ui/theme baddy" \
    ${test_dir}/logfile_access_log.0

check_error_output "config bad theme" <<EOF
error:command-option:1:unknown theme -- baddy
EOF

run_test ${lnav_test} -n \
    -I ${test_dir}/bad-config2 \
    ${test_dir}/logfile_access_log.0

sed -i "" -e "s|/.*/format|format|g" `test_err_filename`

check_error_output "config bad theme" <<EOF
formats/invalid-config/config.json:3:unknown theme -- foo
EOF
