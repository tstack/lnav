#! /bin/bash

export TZ=UTC
export YES_COLOR=1
export DUMP_CRASH=1

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":breakpoint logging_unittest.cc:259" \
    -c "|lnav-moveto-breakpoint next" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ":breakpoint bad" \
    ${test_dir}/logfile_glog.0

# insert a breakpoint via SQL and verify all columns
run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_log_breakpoints (schema_id, description, type) VALUES ('aaaabbbbccccddddeeeeffffaaaabbbb', 'glog_log:foo.cc:42', 'src_location')" \
    -c ";SELECT schema_id, description, type, enabled FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# insert a message_schema breakpoint via SQL
run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_log_breakpoints (schema_id, description, type) VALUES ('11112222333344445555666677778888', 'glog_log:#:0', 'message_schema')" \
    -c ";SELECT schema_id, type FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# update enabled flag via SQL
run_cap_test ${lnav_test} -n \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ";UPDATE lnav_log_breakpoints SET enabled = 0" \
    -c ";SELECT description, enabled FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# delete a breakpoint via SQL
run_cap_test ${lnav_test} -n \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ";DELETE FROM lnav_log_breakpoints" \
    -c ";SELECT count(*) AS remaining FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# insert with invalid type should error
run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_log_breakpoints (schema_id, description, type) VALUES ('aaaabbbbccccddddeeeeffffaaaabbbb', 'glog_log:foo.cc:42', 'bad_type')" \
    ${test_dir}/logfile_glog.0

# insert with schema_id that is too short
run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_log_breakpoints (schema_id, description) VALUES ('abc', 'glog_log:foo.cc:42')" \
    ${test_dir}/logfile_glog.0

# insert with schema_id containing invalid characters
run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_log_breakpoints (schema_id, description) VALUES ('AAAABBBBCCCCDDDDEEEEFFFFAAAABBBB', 'glog_log:foo.cc:42')" \
    ${test_dir}/logfile_glog.0

# update with invalid schema_id
run_cap_test ${lnav_test} -n \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ";UPDATE lnav_log_breakpoints SET schema_id = 'bad'" \
    ${test_dir}/logfile_glog.0

# disable-breakpoint on the current line
run_cap_test ${lnav_test} -n \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ":disable-breakpoint" \
    -c ";SELECT description, enabled FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# disable-breakpoint with a point argument
run_cap_test ${lnav_test} -n \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ":disable-breakpoint logging_unittest.cc:259" \
    -c ";SELECT description, enabled FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# disable-breakpoint when no breakpoint is set
run_cap_test ${lnav_test} -n \
    -c ":disable-breakpoint" \
    ${test_dir}/logfile_glog.0

# enable-breakpoint re-enables a disabled breakpoint
run_cap_test ${lnav_test} -n \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ":disable-breakpoint" \
    -c ":enable-breakpoint" \
    -c ";SELECT description, enabled FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# enable-breakpoint with a point argument re-enables
run_cap_test ${lnav_test} -n \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ":disable-breakpoint logging_unittest.cc:259" \
    -c ":enable-breakpoint logging_unittest.cc:259" \
    -c ";SELECT description, enabled FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# enable-breakpoint creates a new breakpoint if none exists
run_cap_test ${lnav_test} -n \
    -c ":enable-breakpoint logging_unittest.cc:259" \
    -c ";SELECT description, enabled FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

# disable-breakpoint with a non-matching point
run_cap_test ${lnav_test} -n \
    -c ":disable-breakpoint no_such_file.cc:999" \
    ${test_dir}/logfile_glog.0
