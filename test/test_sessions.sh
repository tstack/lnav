#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"

export HOME="./sessions"
mkdir -p $HOME

run_test ${lnav_test} -nSq \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ":goto 1" \
    -c ":partition-name middle" \
    ${test_dir}/logfile_access_log.0

check_output "setting log_mark is not working" <<EOF
EOF

run_test ${lnav_test} -nS \
    -c ':write-to -' \
    ${test_dir}/logfile_access_log.0

check_output "log mark was not saved in session" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -nS \
    -c ';select log_line,log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "partition name was not saved in session" <<EOF
log_line,log_part
0,p.0
1,middle
2,middle
EOF


run_test ${lnav_test} -nSq \
    -c ":adjust-log-time 2010-01-01T00:00:00" \
    ${test_dir}/logfile_access_log.0

check_output "adjust time is not working" <<EOF
EOF


run_test ${lnav_test} -nS \
    ${test_dir}/logfile_access_log.0

check_output "adjust time is not saved in session" <<EOF
192.168.202.254 - - [01/Jan/2010:00:00:00 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [01/Jan/2010:00:00:03 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [01/Jan/2010:00:00:03 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF
