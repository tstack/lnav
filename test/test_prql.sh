#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -n \
    -c ";from access_log | take 1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";from access_log | take abc" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";from bro_http_log | stats.hist bro_host slice:'1m'" \
    ${test_dir}/logfile_bro_http.log.0

# A `let` declaration at the top of a PRQL query defines a helper
# that scopes the rest of the pipeline.  Regression for #1677: the
# interactive preview used to treat the `let` as a standalone
# pipeline stage and insert `take` adjacent to it, producing
# invalid PRQL like `let f = func ... | take 5`.  Submission was
# already correct; this also pins the PRQL stage segmentation that
# the preview relies on.
run_cap_test ${lnav_test} -n \
    -c ";let double = func x -> x * 2
from access_log
derive { d = double sc_bytes }
take 2" \
    ${test_dir}/logfile_access_log.0
