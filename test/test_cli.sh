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

printf "a\ba _\ba a\b_" | run_cap_test env TEST_COMMENT="overstrike bold" \
    ${lnav_test} -n

{
  echo "This is the start of a file with long lines"
  ${lnav_test} -nN \
    -c ";select replicate('abcd', 2 * 1024 * 1024)" -c ':write-raw-to -'
  echo "abcd"
  echo "Goodbye"
} > textfile_long_lines.0

grep abcd textfile_long_lines.0 | run_cap_test \
    ${lnav_test} -n -d /tmp/lnav.err \
    -c ';SELECT filepath, lines FROM lnav_file'
