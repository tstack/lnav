#! /bin/bash

export TZ=UTC
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

run_cap_test ${lnav_test} -n \
    -c ':create-search-table asl_mod ASL Module "(?<name>[^"]+)"' \
    -c ";UPDATE lnav_views SET options = json_object('row-details', 'show') WHERE name = 'log'" \
    -c ":goto 2" \
    ${test_dir}/logfile_syslog.3

run_cap_test ${lnav_test} -n \
    -c ';SELECT * FROM mysql_slow_stats' \
    ${test_dir}/logfile_mysql_slow.0

# A named search in the LOG view gets a search table of the same name.
run_cap_test env TEST_COMMENT='named search table' ${lnav_test} -n \
    -c ':create-named-search gpxe gPXE/(?<ver>[\d\.]+)' \
    -c ';SELECT log_line, ver FROM gpxe' \
    ${test_dir}/logfile_access_log.0

# Disabling the search leaves its table alone.
run_cap_test env TEST_COMMENT='disabled search table' ${lnav_test} -n \
    -c ':create-named-search gpxe gPXE' \
    -c ':disable-named-search gpxe' \
    -c ';SELECT count(*) FROM gpxe' \
    ${test_dir}/logfile_access_log.0

# Deleting the search drops its table.
run_cap_test env TEST_COMMENT='deleted search table' ${lnav_test} -n \
    -c ':create-named-search gpxe gPXE' \
    -c ':delete-named-search gpxe' \
    -c ';SELECT count(*) FROM gpxe' \
    ${test_dir}/logfile_access_log.0

# The table for a named search is driven by the search's own hits, so it has to
# agree with a plain search table for a pattern that matches within a line.
run_cap_test env TEST_COMMENT='named search table matches scan' ${lnav_test} -n \
    -c ':create-named-search gpxe gPXE/(?<ver>[\d\.]+)' \
    -c ':create-search-table gpxe_scan gPXE/(?<ver>[\d\.]+)' \
    -c ';SELECT (SELECT count(*) FROM gpxe) = (SELECT count(*) FROM gpxe_scan) AS same' \
    ${test_dir}/logfile_access_log.0

# A hit on a continuation line still has to bring in its message.
run_cap_test env TEST_COMMENT='named search table continuation' ${lnav_test} -n \
    -c ':create-named-search cont How are' \
    -c ';SELECT log_line FROM cont' \
    ${test_dir}/logfile_multiline.0

# Each match within a message is still its own row.
run_cap_test env TEST_COMMENT='named search table match_index' ${lnav_test} -n \
    -c ':create-named-search segs vmw/(?<seg>\w+)' \
    -c ';SELECT log_line, match_index, seg FROM segs' \
    ${test_dir}/logfile_access_log.0

# The name has to work as a table name.
run_cap_test env TEST_COMMENT='search name not an identifier' ${lnav_test} -n \
    -c ':create-named-search my-search gPXE' \
    ${test_dir}/logfile_access_log.0

# A name that is already taken by a table is refused.
run_cap_test env TEST_COMMENT='search name collides' ${lnav_test} -n \
    -c ':create-named-search access_log gPXE' \
    -c ';SELECT count(*) FROM lnav_view_searches' \
    ${test_dir}/logfile_access_log.0
