#! /bin/bash

export TZ="UTC"
export YES_COLOR=1

run_cap_test ${lnav_test} -n -c 'foo'

run_cap_test ${lnav_test} -d /tmp/lnav.err -n <<EOF
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

export HOME="./piper-config"
rm -rf ./piper-config
mkdir -p $HOME/.lnav

${lnav_test} -Nn -c ':config /tuning/piper/max-size 128'

cat ${test_dir}/logfile_haproxy.0 | run_cap_test \
    env TEST_COMMENT="stdin rotation" ${lnav_test} -n

export HOME="./mgmt-config"
rm -rf ./mgmt-config
mkdir -p $HOME/.lnav
run_cap_test ${lnav_test} -m -I ${test_dir} config get

run_cap_test ${lnav_test} -m -I ${test_dir} config blame

export TMPDIR="piper-tmp"
rm -rf ./piper-tmp
mkdir piper-tmp
run_cap_test ${lnav_test} -n -e 'echo hi'

run_cap_test ${lnav_test} -m piper list

PIPER_URL=$(env NO_COLOR=1 ${lnav_test} -m -q piper list | tail -1 | sed -r -e 's;.*(piper://[^ ]+).*;\1;g')

run_cap_test ${lnav_test} -n $PIPER_URL

run_cap_test ${lnav_test} -n $PIPER_URL \
    -c ";SELECT filepath, descriptor, mimetype, jget(content, '/ctime') as ctime, jget(content, '/cwd') as cwd FROM lnav_file_metadata" \
    -c ':write-json-to -'
