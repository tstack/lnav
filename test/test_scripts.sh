#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"

touch scripts-empty

run_test ${lnav_test} -n -d /tmp/lnav.err \
    -I ${test_dir} \
    -f 'multiline-echo' \
    scripts-empty

check_error_output "multiline-echo has errors?" <<EOF
EOF

check_output "multiline-echo is not working?" <<EOF
Hello, World!
Goodbye, World!
EOF
