#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -n \
    -c ':goto 5' \
    -c ':filter-out Lorem|sed' \
    ${test_dir}/textfile_plain.0

run_cap_test ${lnav_test} -n \
    ${top_srcdir}/README.md

run_cap_test ${lnav_test} -n -c ':goto #screenshot' \
    ${top_srcdir}/README.md

run_cap_test ${lnav_test} -n ${top_srcdir}/README.md#screenshot

# run_cap_test ${lnav_test} -n ${test_dir}/non-existent:4

run_cap_test ${lnav_test} -n ${top_srcdir}/README.md:-4

run_cap_test ${lnav_test} -n \
    -c ':goto 115' \
    -c ";SELECT top_meta FROM lnav_views WHERE name = 'text'" \
    -c ':write-json-to -' \
    ${top_srcdir}/README.md

run_cap_test ${lnav_test} -n \
    ${top_srcdir}/src/log_level.cc

cp ${test_dir}/UTF-8-test.txt UTF-8-test.md
run_cap_test ${lnav_test} -n \
    UTF-8-test.md

run_cap_test ${lnav_test} -n \
    -c ';SELECT * FROM lnav_file_metadata' \
    ${test_dir}/textfile_0.md

run_cap_test ${lnav_test} -n \
    ${test_dir}/textfile_ansi_expanding.0

run_cap_test ${lnav_test} -n \
    ${test_dir}/textfile_0.md

run_cap_test ${lnav_test} -n \
    ${test_dir}/pyfile_0.py

run_cap_test ${lnav_test} -n \
    ${test_dir}/man_echo.txt

run_cap_test ${lnav_test} -n \
    -c ";SELECT top_meta FROM lnav_views WHERE name = 'text'" \
    -c ':write-json-to -' \
    ${test_dir}/man_echo.txt

run_cap_test ${lnav_test} -n \
    -c ':goto 8' \
    -c ";SELECT top_meta FROM lnav_views WHERE name = 'text'" \
    -c ':write-json-to -' \
    < ${test_dir}/man_echo.txt

run_cap_test ${lnav_test} -n \
    -c ':goto 6' \
    -c ";SELECT top_meta FROM lnav_views WHERE name = 'text'" \
    -c ':write-json-to -' \
    < ${test_dir}/example.toml

run_cap_test ${lnav_test} -n \
    -c ':goto 9' \
    -c ";SELECT top_meta FROM lnav_views WHERE name = 'text'" \
    -c ':write-json-to -' \
    < ${test_dir}/example.patch

run_cap_test ${lnav_test} -n \
    < ${top_srcdir}/autogen.sh

run_cap_test ${lnav_test} -n \
    -c ';SELECT content FROM lnav_file' \
    ${test_dir}/textfile_nonl.txt

run_cap_test ${lnav_test} -n \
    -c ':goto 23' \
    -c ';SELECT top_meta FROM lnav_top_view' \
    -c ':write-json-to -' \
    ${test_dir}/formats/jsontest/format.json

run_cap_test ${lnav_test} -n \
    -c ':goto 3' \
    -c ':next-section' \
    ${test_dir}/books.json

run_cap_test ${lnav_test} -n \
    -c ':goto 3' \
    -c ':next-section' \
    < ${test_dir}/books.json

run_cap_test ${lnav_test} -n \
    -c ':goto #/catalog/1/title' \
    ${test_dir}/books.json

run_cap_test ${lnav_test} -n \
    -c ':goto #/catalog/1/title' \
    < ${test_dir}/books.json

echo "Hello, World!" | run_cap_test env TEST_COMMENT="piper crumbs" ${lnav_test} -n \
    -c ';SELECT top_meta FROM lnav_top_view' \
    -c ':write-json-to -'

echo "Hello, World!" | run_cap_test \
    env TEST_COMMENT="piper crumbs" TZ=America/Los_Angeles \
    ${lnav_test} -n \
    -c ';SELECT top_meta FROM lnav_top_view' \
    -c ':write-json-to -'

echo "Hello, World!" | run_cap_test \
    env TEST_COMMENT="piper crumbs" TZ=America/Los_Angeles \
    ${lnav_test} -nt

echo "Hello, World!" | run_cap_test \
    env TEST_COMMENT="piper time offset" TZ=America/Los_Angeles \
    ${lnav_test} -n \
    -c ";UPDATE lnav_views SET options = json_object('row-time-offset', 'show') WHERE name = 'text'"

${test_dir}/naughty_files.py
run_cap_test ${lnav_test} -n naughty/file-with-hidden-text.txt

run_cap_test ${lnav_test} -n naughty/file-with-terminal-controls.txt

run_cap_test ${lnav_test} -nN \
    -c ':set-text-view-mode'

run_cap_test ${lnav_test} -nN \
    -c ':set-text-view-mode blah'

run_cap_test ${lnav_test} -n \
    -c ':set-text-view-mode raw' \
    ${top_srcdir}/README.md

run_cap_test ${lnav_test} -n \
    ${test_dir}/textfile_json_long.0

run_cap_test ${lnav_test} -nN \
    -c ':switch-to-view help' \
    -c ':next-section' \
    -c ':next-section' \
    -c ";SELECT top FROM lnav_views WHERE name = 'help'"

run_cap_test ${lnav_test} -n \
    ${top_srcdir}/src/scripts/lnav-pop-view.lnav
