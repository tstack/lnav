#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -n \
    -c ';SELECT * FROM procstate_procs' \
    ${test_dir}/logfile_procstate.0

run_cap_test ${lnav_test} -n \
    -c ';SELECT *,log_body FROM vpx_lro_begin' \
    ${test_dir}/logfile_vpxd.0

run_cap_test ${lnav_test} -n \
    -c ";select * from vpx_lro_begin where log_line > 3 and lro_id = 'lro-846064'" \
    -c ";select * from vpx_lro_begin where lro_id = 'lro-846064'" \
    ${test_dir}/logfile_vpxd.0

run_cap_test ${lnav_test} -n \
    -c ";select * from procstate_procs where cmd_name = '[kthreadd]'" \
    -c ";select * from procstate_procs where cmd_name = '[kthreadd]'" \
    ${test_dir}/logfile_procstate.0

touch -t 202211030923 ${test_dir}/logfile_syslog.3

run_cap_test ${lnav_test} -n \
    -c ':create-search-table asl_mod ASL Module "(?<name>[^"]+)"' \
    -c ';SELECT * FROM asl_mod' \
    ${test_dir}/logfile_syslog.3
