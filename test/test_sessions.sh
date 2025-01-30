#! /bin/bash

export TZ=UTC
export HOME="./sessions"
unset XDG_CONFIG_HOME
rm -rf "./sessions"
mkdir -p $HOME

run_cap_test ${lnav_test} -n \
    -c ":reset-session" \
    -c ":goto 0" \
    -c ":hide-file" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.*

# hidden file saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    ${test_dir}/logfile_access_log.*

# setting log_mark
run_cap_test ${lnav_test} -nq \
    -c ":reset-session" \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

mkdir -p support-dump
echo 'Hello' > support-dump/readme
cp ${test_dir}/logfile_access_log.0 support-dump/
cp ${test_dir}/logfile_access_log.1 support-dump/

run_cap_test ${lnav_test} -nq \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ":goto 1" \
    -c ":hide-file */logfile_access_log.1" \
    -c ":export-session-to -" \
    support-dump/logfile_access_log.*

run_cap_test ${lnav_test} -nq \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ":hide-fields cs_user_agent" \
    -c ":set-min-log-level debug" \
    -c ":hide-lines-before 2005" \
    -c ":hide-lines-after 2030" \
    -c ":filter-out blah" \
    -c "/foobar" \
    -c ":goto 1" \
    -c ":export-session-to exported-session.0.lnav" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c "|exported-session.0.lnav" \
    -c ";SELECT * FROM lnav_view_filters" \
    -c ":write-screen-to -" \
    -c ";SELECT name,search FROM lnav_views" \
    -c ":write-screen-to -" \
    ${test_dir}/logfile_access_log.0

# log mark was not saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ':write-to -' \
    ${test_dir}/logfile_access_log.0

# file was not closed
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":close" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# partition name was not saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ';select log_line,log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

# partition name was not saved in session and doesn't not show up in crumbs
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ':goto 2' \
    -c ";SELECT top_meta FROM lnav_views WHERE name = 'log'" \
    -c ':write-json-to -' \
    ${test_dir}/logfile_access_log.0

# adjust time is not working
run_cap_test ${lnav_test} -nq \
    -c ":adjust-log-time 2010-01-01T00:00:00" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# adjust time is not saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":test-comment adjust time in session" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":reset-session" \
    -c ":save-session" \
    -c ":test-comment reset adjusted time" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":test-comment reset of adjust stuck" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":adjust-log-time +1h" \
    -c ":reset-session" \
    ${test_dir}/logfile_java.*

# hiding fields failed
rm -rf ./sessions
mkdir -p $HOME
run_cap_test ${lnav_test} -nq -d /tmp/lnav.err \
    -c ":hide-fields c_ip" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# restoring hidden fields failed
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":test-comment restoring hidden fields" \
    ${test_dir}/logfile_access_log.0

# hiding fields failed
rm -rf ./sessions
mkdir -p $HOME
run_cap_test ${lnav_test} -nq -d /tmp/lnav.err \
    -c ":hide-lines-before 2009-07-20 22:59:29" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# XXX we don't actually check
# restoring hidden fields failed
run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":load-session" \
    -c ":test-comment restore hidden lines" \
    ${test_dir}/logfile_access_log.0

# hiding fields failed
export TZ="UTC"
rm -rf ./sessions
mkdir -p $HOME
run_cap_test ${lnav_test} -n \
    -c ":hide-fields bro_uid" \
    -c ":goto -10" \
    -c ":save-session" \
    ${test_dir}/logfile_bro_http.log.0

# restoring hidden fields failed
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":goto -10" \
    -c ":test-comment restoring hidden fields" \
    ${test_dir}/logfile_bro_http.log.0

export TEST_ANNO=1
run_cap_test ${lnav_test} -d /tmp/lnav.err -I ${test_dir} -n \
    -c ':annotate' \
    -c ':save-session' \
    support-dump/logfile_access_log.0

run_cap_test ${lnav_test} -d /tmp/lnav.err -I ${test_dir} -n \
    -c ':load-session' \
    -c ':export-session-to -' \
    support-dump/logfile_access_log.0
