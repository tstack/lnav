#! /bin/bash

export TZ="UTC"

run_test ${lnav_test} -n -c 'foo'

check_error_output "invalid command not detected?" <<EOF
✘ error: invalid value for “-c” option
 --> arg
 |  -c foo
 |     ^ command type prefix is missing
 = help: command arguments must start with one of the following symbols to denote the type of command:
            : - an lnav command   (e.g. :goto 42)
            ; - an SQL statement  (e.g. SELECT * FROM syslog_log)
            | - an lnav script    (e.g. |rename-stdin foo)
EOF

run_test ${lnav_test} -d /tmp/lnav.err -t -n <<EOF
Hello, World!
Goodbye, World!
EOF

check_output "stdin timestamping not working?" <<EOF
2013-06-06T19:13:20.123  Hello, World!
2013-06-06T19:13:20.123  Goodbye, World!
2013-06-06T19:13:20.123  ---- END-OF-STDIN ----
EOF

mkdir -p nested/sub1/sub2
echo "2021-07-03T21:49:29 Test" > nested/sub1/sub2/test.log

run_test ${lnav_test} -nr nested

check_output "recursive open not working?" <<EOF
2021-07-03T21:49:29 Test
EOF
