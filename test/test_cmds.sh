#! /bin/bash

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
    -c ":goto invalid" \
    ${test_dir}/logfile_access_log.0

check_error_output "goto invalid is working" <<EOF
error: expecting line number/percentage or timestamp
EOF

check_output "goto invalid is not working" <<EOF
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
    -c ":close" \
    ${test_dir}/logfile_access_log.0

check_error_output "double close works" <<EOF
error: no log files loaded
EOF

check_output "double close is working" <<EOF
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


run_test ${lnav_test} -n \
    -c ":close" \
    -c ":open /non-existent" \
    ${test_dir}/logfile_access_log.0

check_error_output "open non-existent is working" <<EOF
error: cannot stat file: /non-existent -- No such file or directory
EOF

check_output "open non-existent is not working" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":close" \
    -c ":goto 0" \
    "${test_dir}/logfile_access_log.*"

check_output "close not sticking" <<EOF
10.112.81.15 - - [15/Feb/2013:06:00:31 +0000] "-" 400 0 "-" "-"
EOF


run_test ${lnav_test} -n \
    -c ";select * from access_log" \
    -c ':write-json-to -' \
    ${test_dir}/logfile_access_log.0

check_output "write-json-to is not working" <<EOF
[
    {
        "log_line": 0,
        "log_part": "p.0",
        "log_time": "2009-07-20 22:59:26.000",
        "log_idle_msecs": 0,
        "log_level": "info",
        "log_mark": 0,
        "c_ip": "192.168.202.254",
        "cs_method": "GET",
        "cs_referer": "-",
        "cs_uri_query": null,
        "cs_uri_stem": "/vmw/cgi/tramp",
        "cs_user_agent": "gPXE/0.9.7",
        "cs_username": "-",
        "cs_version": "HTTP/1.0",
        "sc_bytes": 134,
        "sc_status": 200
    },
    {
        "log_line": 1,
        "log_part": "p.0",
        "log_time": "2009-07-20 22:59:29.000",
        "log_idle_msecs": 3000,
        "log_level": "error",
        "log_mark": 0,
        "c_ip": "192.168.202.254",
        "cs_method": "GET",
        "cs_referer": "-",
        "cs_uri_query": null,
        "cs_uri_stem": "/vmw/vSphere/default/vmkboot.gz",
        "cs_user_agent": "gPXE/0.9.7",
        "cs_username": "-",
        "cs_version": "HTTP/1.0",
        "sc_bytes": 46210,
        "sc_status": 404
    },
    {
        "log_line": 2,
        "log_part": "p.0",
        "log_time": "2009-07-20 22:59:29.000",
        "log_idle_msecs": 0,
        "log_level": "info",
        "log_mark": 0,
        "c_ip": "192.168.202.254",
        "cs_method": "GET",
        "cs_referer": "-",
        "cs_uri_query": null,
        "cs_uri_stem": "/vmw/vSphere/default/vmkernel.gz",
        "cs_user_agent": "gPXE/0.9.7",
        "cs_username": "-",
        "cs_version": "HTTP/1.0",
        "sc_bytes": 78929,
        "sc_status": 200
    }
]
EOF


run_test ${lnav_test} -n \
    -c ":set-min-log-level error" \
    ${test_dir}/logfile_access_log.0

check_output "set-min-log-level is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -d/tmp/lnav.err -n \
    -c ":highlight foobar" \
    -c ":clear-highlight foobar" \
    ${test_dir}/logfile_access_log.0

check_error_output "clear-highlight is not working?" <<EOF
EOF

run_test ${lnav_test} -d/tmp/lnav.err -n \
    -c ":clear-highlight foobar" \
    ${test_dir}/logfile_access_log.0

check_error_output "clear-highlight did not report an error?" <<EOF
error: highlight does not exist
EOF
