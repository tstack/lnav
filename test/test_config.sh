#!/usr/bin/env bash

export HOME="./test-config"
rm -rf ./test-config
mkdir -p $HOME/.config

run_test ${lnav_test} -nN \
    -c ":config /global/foo bar"

check_output "config write global var" <<EOF
EOF

run_test ${lnav_test} -nN \
    -c ":config /global/foo"

check_output "config read global var" <<EOF
/global/foo = "foo"
EOF

run_test ${lnav_test} -n \
    -c ":config /ui/theme-defs/default/styles/text/color #f" \
    ${test_dir}/logfile_access_log.0

check_error_output "config bad color" <<EOF
error:command-option:1:Could not parse color: #f
EOF

run_test env TMPDIR=tmp ${lnav_test} -n \
    -c ':config /tuning/archive-manager/min-free-space abc' \
    ${srcdir}/logfile_syslog.0

check_error_output "invalid min-free-space allowed?" <<EOF
command-option:1: error: expecting an integer, found: abc
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

check_error_output "config bad theme" <<EOF
warning:{test_dir}/bad-config2/formats/invalid-config/config.malformed.json:line 2
warning:  unexpected path --
warning:    /ui
warning:  accepted paths --
warning:    \$schema The URI of the schema for this file -- Specifies the type of this file
warning:    tuning  -- Internal settings
warning:    ui  -- User-interface settings
warning:    global  -- Global variable definitions
warning:{test_dir}/bad-config2/formats/invalid-config/config.truncated.json:line 2
warning:  unexpected path --
warning:    /ui
warning:  accepted paths --
warning:    \$schema The URI of the schema for this file -- Specifies the type of this file
warning:    tuning  -- Internal settings
warning:    ui  -- User-interface settings
warning:    global  -- Global variable definitions
error:{test_dir}/bad-config2/formats/invalid-config/config.malformed.json:3:invalid json -- parse error: object key and value must be separated by a colon (':')
               "ui": "theme",     "abc",     "def": "" }
                     (right here) ------^
error:{test_dir}/bad-config2/formats/invalid-config/config.truncated.json: invalid json -- parse error: premature EOF
EOF
