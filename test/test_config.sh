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
warning:formats/invalid-config/config.malformed.json:line 2
warning:  unexpected path --
warning:    /ui
warning:  accepted paths --
warning:    /ui/  -- User-interface settings
warning:formats/invalid-config/config.truncated.json:line 2
warning:  unexpected path --
warning:    /ui
warning:  accepted paths --
warning:    /ui/  -- User-interface settings
error:formats/invalid-config/config.malformed.json:3:invalid json -- parse error: object key and value must be separated by a colon (':')
               "ui": "theme",     "abc",     "def": "" }
                     (right here) ------^
error:formats/invalid-config/config.truncated.json: invalid json -- parse error: premature EOF
formats/invalid-config/config.json:3:unknown theme -- foo
EOF
