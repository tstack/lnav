#! /bin/bash

export TZ="UTC"
run_test ${lnav_test} -t -n <<EOF
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
