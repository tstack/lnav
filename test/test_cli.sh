#! /bin/bash

run_test ${lnav_test} -t -n <<EOF
Hello, World!
Goodbye, World!
EOF

check_output "file URL is not working" <<EOF
2013-06-06T12:13:20.123  Hello, World!
2013-06-06T12:13:20.123  Goodbye, World!
2013-06-06T12:13:20.123  ---- END-OF-STDIN ----
EOF
