#! /bin/bash

export YES_COLOR=1
unset XDG_CONFIG_HOME

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
