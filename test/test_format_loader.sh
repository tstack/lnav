#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -C \
    -I ${test_dir}/bad-config

sed -i "" -e "s|/.*/init.sql|init.sql|g" `test_err_filename`

check_error_output "invalid format not detected?" <<EOF
error:bad_regex_log.regex[std]:missing )
error:bad_regex_log.level:missing )
error:bad_regex_log:invalid sample -- 1428634687123; foo
error:bad_sample_log:invalid sample -- 1428634687123; foo bar
error:bad_sample_log:partial sample matched -- 1428634687123; foo
error:  against pattern -- ^(?<timestamp>\d+); (?<body>\w+)$
error:bad_sample_log:partial sample matched -- 1428634687123
error:  against pattern -- ^(?<timestamp>\d+): (?<body>.*)$
error:no_sample_log:no sample logs provided, all formats must have samples
error:init.sql:2:near "TALE": syntax error
EOF
