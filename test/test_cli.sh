#! /bin/bash

export TZ="UTC"
export YES_COLOR=1

run_cap_test ${lnav_test} -n -c 'foo'

run_cap_test ${lnav_test} -d /tmp/lnav.err -t -n <<EOF
Hello, World!
Goodbye, World!
EOF

mkdir -p nested/sub1/sub2
echo "2021-07-03T21:49:29 Test" > nested/sub1/sub2/test.log

run_cap_test ${lnav_test} -nr nested
