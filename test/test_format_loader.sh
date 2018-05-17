#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -C \
    -I ${test_dir}/bad-config-json

sed -i "" -e "s|/.*/format|format|g" `test_err_filename`

check_error_output "invalid format not detected?" <<EOF
warning:format.json:line 5
warning:  unexpected path --
warning:    /invalid_key_log/value/test/identifiers
warning:  accepted paths --
warning:    kind string|integer|float|boolean|json|quoted -- The type of data in the field
warning:    collate <function> -- The collating function to use for this column
warning:    unit/  -- Unit definitions for this field
warning:    identifier <bool> -- Indicates whether or not this field contains an identifier that should be highlighted
warning:    foreign-key <bool> -- Indicates whether or not this field should be treated as a foreign key for row in another table
warning:    hidden <bool> -- Indicates whether or not this field should be hidden
warning:    action-list# <string> -- Actions to execute when this field is clicked on
warning:    rewriter <command> -- A command that will rewrite this field when pretty-printing
warning:    description <string> -- A description of the field
error:format.json:4:invalid json -- parse error: object key and value must be separated by a colon (':')
          ar_log": {         "abc"     } }
                     (right here) ------^
EOF

run_test ${lnav_test} -C \
    -I ${test_dir}/bad-config

sed -i "" -e "s|/.*/init.sql|init.sql|g" `test_err_filename`

check_error_output "invalid format not detected?" <<EOF
error:bad_regex_log.regex[std]:missing )
error:bad_regex_log.regex[std]:^(?<timestamp>\d+: (?<body>.*)$
error:bad_regex_log.regex[std]:                               ^
error:bad_regex_log.level:missing )
error:bad_regex_log:invalid sample -- 1428634687123; foo
error:bad_regex_log:highlighters/foobar:missing )
error:bad_regex_log:highlighters/foobar:abc(
error:bad_regex_log:highlighters/foobar:    ^
error:bad_sample_log:invalid sample -- 1428634687123; foo bar
error:bad_sample_log:partial sample matched -- 1428634687123; foo
error:  against pattern -- ^(?<timestamp>\d+); (?<body>\w+)$
error:bad_sample_log:partial sample matched -- 1428634687123
error:  against pattern -- ^(?<timestamp>\d+): (?<body>.*)$
error:no_sample_log:no sample logs provided, all formats must have samples
error:init.sql:2:near "TALE": syntax error
EOF

run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";select * from leveltest_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_leveltest.0

check_output "levels are not correct?" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags
0,<NULL>,2016-06-30 12:00:01.000,0,trace,0,<NULL>,<NULL>
1,<NULL>,2016-06-30 12:00:02.000,1000,debug,0,<NULL>,<NULL>
2,<NULL>,2016-06-30 12:00:03.000,1000,debug2,0,<NULL>,<NULL>
3,<NULL>,2016-06-30 12:00:04.000,1000,debug3,0,<NULL>,<NULL>
4,<NULL>,2016-06-30 12:00:05.000,1000,info,0,<NULL>,<NULL>
5,<NULL>,2016-06-30 12:00:06.000,1000,warning,0,<NULL>,<NULL>
6,<NULL>,2016-06-30 12:00:07.000,1000,fatal,0,<NULL>,<NULL>
7,<NULL>,2016-06-30 12:00:08.000,1000,info,0,<NULL>,<NULL>
EOF
