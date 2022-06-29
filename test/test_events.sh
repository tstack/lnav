#! /bin/bash

rm -rf events-home
mkdir -p events-home
export HOME=events-home
export YES_COLOR=1

run_cap_test ${lnav_test} -n \
   -c ';SELECT json(content) as content FROM lnav_events' \
   -c ':write-jsonlines-to -' \
   ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -nN \
   -c ':config /log/watch-expressions/http-errors/expr sc_status >= 400 AND bad'

run_cap_test ${lnav_test} -nN \
   -c ':config /log/watch-expressions/http-errors/expr :sc_status >= 400'

run_cap_test env TEST_COMMENT="watch expression generate detect event" ${lnav_test} -n \
   -c ';SELECT json(content) as content FROM lnav_events' \
   -c ':write-jsonlines-to -' \
   ${test_dir}/logfile_access_log.0

run_cap_test env TEST_COMMENT="show the configuration" ${lnav_test} -nN \
   -c ':config /log/watch-expressions'

run_cap_test env TEST_COMMENT="delete the configuration" ${lnav_test} -nN \
   -c ':reset-config /log/watch-expressions/http-errors/'

run_cap_test env TEST_COMMENT="config should be gone now" ${lnav_test} -nN \
   -c ':config /log/watch-expressions'
