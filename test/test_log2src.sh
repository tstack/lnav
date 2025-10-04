#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/python" \
    -c ";SELECT log_line, log_msg_src, log_msg_format, log_msg_values FROM all_logs" \
    -c ":write-jsonlines-to -" \
    ${test_dir}/log2src/python/python-example.0

run_cap_test ${lnav_test} -nN \
    -c ":add-source-path ${test_dir}/log2src/python" \
    -c ";SELECT * FROM source_log_stmt('log_example.py')" \
    -c ":write-jsonlines-to -"
