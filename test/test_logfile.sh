#! /bin/sh

run_test ./drive_logfile -f syslog_log ${srcdir}/logfile_syslog.0

on_error_fail_with "Didn't infer syslog log format?"

run_test ./drive_logfile -f tcsh_history ${srcdir}/logfile_tcsh_history.0

on_error_fail_with "Didn't infer tcsh-history log format?"

run_test ./drive_logfile -f access_log ${srcdir}/logfile_access_log.0

on_error_fail_with "Didn't infer access_log log format?"


run_test ./drive_logfile ${srcdir}/logfile_empty.0

on_error_fail_with "Didn't handle empty log?"


run_test ./drive_logfile -t -f syslog_log ${srcdir}/logfile_syslog.0

check_output "Syslog timestamp interpreted incorrectly?" <<EOF
Nov 03 09:23:38 2007 -- 000
Nov 03 09:23:38 2007 -- 000
Nov 03 09:23:38 2007 -- 000
Nov 03 09:47:02 2007 -- 000
EOF

run_test ./drive_logfile -t -f syslog_log ${srcdir}/logfile_syslog.1

check_output "Syslog timestamp interpreted incorrectly for year end?" <<EOF
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Jan 03 09:47:02 2007 -- 000
EOF

run_test ./drive_logfile -t -f tcsh_history ${srcdir}/logfile_tcsh_history.0

check_output "TCSH timestamp interpreted incorrectly?" <<EOF
Nov 02 09:59:26 2006 -- 000
Nov 02 09:59:26 2006 -- 000
Nov 02 09:59:45 2006 -- 000
Nov 02 09:59:45 2006 -- 000
EOF

run_test ./drive_logfile -t -f access_log ${srcdir}/logfile_access_log.0

check_output "access_log timestamp interpreted incorrectly?" <<EOF
Jul 20 22:59:26 2009 -- 000
Jul 20 22:59:29 2009 -- 000
Jul 20 22:59:29 2009 -- 000
EOF

##

run_test ./drive_logfile -v -f syslog_log ${srcdir}/logfile_syslog.0

check_output "Syslog level interpreted incorrectly?" <<EOF
0x05
0x00
0x05
0x00
EOF

run_test ./drive_logfile -v -f tcsh_history ${srcdir}/logfile_tcsh_history.0

check_output "TCSH level interpreted incorrectly?" <<EOF
0x40
0xc0
0x40
0xc0
EOF

run_test ./drive_logfile -v -f access_log ${srcdir}/logfile_access_log.0

check_output "access_log level interpreted incorrectly?" <<EOF
0x03
0x05
0x03
EOF
