#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"

export HOME="./meta-sessions"
rm -rf "./meta-sessions"
mkdir -p $HOME

run_test ${lnav_test} -n \
    -c ":comment Hello, World!" \
    -c ":tag foo" \
    -c ":save-session" \
    -c ":write-screen-to -" \
    ${test_dir}/logfile_access_log.0

check_output ":tag did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
  // Hello, World!
  -- #foo
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";UPDATE access_log SET log_mark = 1" \
    -c ":write-to -" \
    ${test_dir}/logfile_access_log.0

check_output "tag was not saved in session?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
  // Hello, World!
  -- #foo
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":untag #foo" \
    ${test_dir}/logfile_access_log.0

check_output ":untag did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
 + Hello, World!
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":clear-comment" \
    ${test_dir}/logfile_access_log.0

check_output ":clear-comment did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
 + #foo
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":goto 2" \
    -c "/foo" \
    -c ":tag #foo" \
    -c ":goto 0" \
    -c ":next-mark search" \
    ${test_dir}/logfile_access_log.0

check_output "searching for a tag did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
  + #foo
EOF

run_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";SELECT log_line, log_comment, log_tags FROM access_log" \
    ${test_dir}/logfile_access_log.0

check_output "metadata columns are not working?" <<EOF
log_line  log_comment  log_tags
       0 Hello, World! ["#foo"]
       1 <NULL>        <NULL>
       2 <NULL>        <NULL>
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE access_log SET log_tags = json_array('#foo') WHERE log_line = 1" \
    ${test_dir}/logfile_access_log.0

check_output "updating log_tags is not working?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
 + #foo
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE access_log SET log_comment = 'Hello, World!' WHERE log_line = 1" \
    ${test_dir}/logfile_access_log.0

check_output "updating log_comment is not working?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
 + Hello, World!
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE access_log SET log_tags = 1 WHERE log_line = 1" \
    ${test_dir}/logfile_access_log.0

check_error_output "updating log_tags is not working?" <<EOF
error:command-option:1:command-option:line 1
  unexpected JSON value
  accepted paths --
    # <tag> -- A tag for the log line
EOF

run_test ${lnav_test} -n \
    -c ":tag foo" \
    -c ";UPDATE access_log SET log_tags = null" \
    ${test_dir}/logfile_access_log.0

check_output "clearing log_tags is not working?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":comment foo" \
    -c ";UPDATE access_log SET log_comment = null" \
    ${test_dir}/logfile_access_log.0

check_output "clearing log_tags is not working?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF
