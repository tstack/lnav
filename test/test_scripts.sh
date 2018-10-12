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

run_test ${lnav_test} -n -d /tmp/lnav.err \
    -I ${test_dir} \
    -f 'redirecting' \
    scripts-empty

check_error_output "redirecting has errors?" <<EOF
EOF

check_output "redirecting is not working?" <<EOF
Howdy!
Goodbye, World!
EOF

diff -w -u - hw.txt <<EOF
Hello, World!
HOWDY!
GOODBYE, WORLD!
EOF

if test $? -ne 0; then
    echo "Script output was not redirected?"
    exit 1
fi

diff -w -u - hw2.txt <<EOF
HELLO, WORLD!
EOF

if test $? -ne 0; then
    echo "Script output was not redirected?"
    exit 1
fi
