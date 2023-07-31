#! /bin/bash

export YES_COLOR=1

export HOME="./meta-sessions"
export XDG_CONFIG_HOME="./meta-sessions/.config"
rm -rf "./meta-sessions"
mkdir -p $HOME/.config

# add comment/tag
run_cap_test ${lnav_test} -n -dln.dbg \
    -c ":comment Hello, World!" \
    -c ":tag foo" \
    -c ":save-session" \
    -c ":write-screen-to -" \
    ${test_dir}/logfile_access_log.0

ls -lha meta-sessions
find meta-sessions
# cat ln.dbg
if test ! -d meta-sessions/.config/lnav; then
    echo "error: configuration not stored in .config/lnav?"
    exit 1
fi

if test -d meta-sessions/.lnav; then
    echo "error: configuration stored in .lnav?"
    exit 1
fi

# tag was saved and :write-to displays the comments/tags
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";UPDATE access_log SET log_mark = 1" \
    -c ":write-to -" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":untag #foo" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":clear-comment" \
    ${test_dir}/logfile_access_log.0

# search for a tag
run_cap_test ${lnav_test} -n \
    -c ":goto 2" \
    -c "/foo" \
    -c ":tag #foo" \
    -c ":goto 0" \
    -c ":next-mark search" \
    ${test_dir}/logfile_access_log.0

# query meta columns
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";SELECT log_line, log_comment, log_tags FROM access_log" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE access_log SET log_tags = json_array('#foo', '#foo') WHERE log_line = 1" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE access_log SET log_comment = 'Goodbye, World!' WHERE log_line = 1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE access_log SET log_tags = 1 WHERE log_line = 1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE access_log SET log_tags = json_array('foo') WHERE log_line = 1" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";SELECT log_tags FROM access_log WHERE log_line = 1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":tag foo" \
    -c ":delete-tags #foo" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":tag foo" \
    -c ";UPDATE access_log SET log_tags = null" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":comment foo" \
    -c ";UPDATE access_log SET log_comment = null" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -d /tmp/lnav.err -n \
    -I ${test_dir} \
    ${test_dir}/logfile_xml_msg.0

run_cap_test ${lnav_test} -n -f- \
    ${test_dir}/logfile_access_log.0 <<'EOF'
:comment Hello, **World**!

This is `markdown` now!
EOF

export TEST_ANNO=1
run_cap_test ${lnav_test} -d /tmp/lnav.err -I ${test_dir} -n \
    -c ':annotate' \
    ${test_dir}/logfile_access_log.0
