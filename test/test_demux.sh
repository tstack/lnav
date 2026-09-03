#! /bin/bash

export YES_COLOR=1
export TZ=UTC

# The stdin captures this test makes get demuxed into per-container files.
# lnav's work directory is keyed on the uid alone, so without a private one
# those show up in another test that happens to be reading stdin at the same
# time -- the suite runs its test files in parallel.
export LNAV_WORK_DIR="${PWD}/demux-work"
rm -rf "${LNAV_WORK_DIR}"
mkdir -p "${LNAV_WORK_DIR}"

cat ${test_dir}/logfile_docker_compose.0 | run_cap_test env TEST_COMMENT="docker-demux-no-ts" \
     ${lnav_test} -n

cat ${test_dir}/logfile_docker_compose_with_ts.0 | run_cap_test env TEST_COMMENT="docker-demux-with-ts" \
     ${lnav_test} -n

run_cap_test ${lnav_test} -n ${test_dir}/logfile_docker_compose_with_ts.0

run_cap_test ${lnav_test} -n ${test_dir}/logfile_mux_zookeeper.0

run_cap_test ${lnav_test} -n \
    -c ';SELECT * FROM lnav_file_demux_metadata' \
    ${test_dir}/logfile_mux_zookeeper.0

run_cap_test ${lnav_test} -n ${test_dir}/logfile_docker_compose_with_noise.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view text' \
    ${test_dir}/logfile_docker_compose_with_noise.0

run_cap_test ${lnav_test} -n ${test_dir}/logfile_muxed_syslog.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM syslog_log" \
    ${test_dir}/logfile_muxed_syslog.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM lnav_file_demux_metadata ORDER BY filepath ASC" \
    ${test_dir}/logfile_muxed_syslog.0
