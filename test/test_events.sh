#! /bin/bash

run_cap_test ${lnav_test} -n \
   -c ';SELECT json(content) as content FROM lnav_events' \
   -c ':write-jsonlines-to -' \
   ${test_dir}/logfile_access_log.0
