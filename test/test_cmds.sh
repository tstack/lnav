#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -n \
    -c ":adjust-log-time 2010-01-01T00:00:00" \
    ${test_dir}/logfile_access_log.0

check_output "adjust-log-time is not working" <<EOF
192.168.202.254 - - [01/Jan/2010:00:00:00 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [01/Jan/2010:00:00:03 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [01/Jan/2010:00:00:03 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ":goto 1" \
    ${test_dir}/logfile_access_log.0

check_output "goto 1 is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ":goto -1" \
    ${test_dir}/logfile_access_log.0

check_output "goto -1 is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ":filter-in vmk" \
    ${test_dir}/logfile_access_log.0

check_output "filter-in vmk is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ":filter-in today" \
    ${test_dir}/logfile_multiline.0

check_output "filter-in multiline is not working" <<EOF
2009-07-20 22:59:27,672:DEBUG:Hello, World!
  How are you today?
EOF


run_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    ${test_dir}/logfile_access_log.0

check_output "filter-out vmk is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ":filter-out today" \
    ${test_dir}/logfile_multiline.0

check_output "filter-out multiline is not working" <<EOF
2009-07-20 22:59:30,221:ERROR:Goodbye, World!
EOF


run_test ${lnav_test} -n \
    -c ":switch-to-view help" \
    ${test_dir}/logfile_access_log.0

check_output "switch-to-view help is not working" < ${top_srcdir}/src/help.txt


run_test ${lnav_test} -n \
    -c ":close" \
    ${test_dir}/logfile_access_log.0

check_output "close is not working" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ":close" \
    -c ":open ${test_dir}/logfile_multiline.0" \
    ${test_dir}/logfile_access_log.0

check_output "open is not working" <<EOF
2009-07-20 22:59:27,672:DEBUG:Hello, World!
  How are you today?
2009-07-20 22:59:30,221:ERROR:Goodbye, World!
EOF
