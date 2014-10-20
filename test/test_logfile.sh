#! /bin/bash

run_test ./drive_logfile -f syslog_log ${srcdir}/logfile_syslog.0

on_error_fail_with "Didn't infer syslog log format?"

run_test ./drive_logfile -f tcsh_history ${srcdir}/logfile_tcsh_history.0

on_error_fail_with "Didn't infer tcsh-history log format?"

run_test ./drive_logfile -f access_log ${srcdir}/logfile_access_log.0

on_error_fail_with "Didn't infer access_log log format?"

run_test ./drive_logfile -f strace_log ${srcdir}/logfile_strace_log.0

on_error_fail_with "Didn't infer strace_log log format?"


run_test ./drive_logfile ${srcdir}/logfile_empty.0

on_error_fail_with "Didn't handle empty log?"

touch -t 200711030923 ${srcdir}/logfile_syslog.0
run_test ./drive_logfile -t -f syslog_log ${srcdir}/logfile_syslog.0

check_output "Syslog timestamp interpreted incorrectly?" <<EOF
Nov 03 09:23:38 2007 -- 000
Nov 03 09:23:38 2007 -- 000
Nov 03 09:23:38 2007 -- 000
Nov 03 09:47:02 2007 -- 000
EOF

touch -t 200711030923 ${srcdir}/logfile_syslog.1
run_test ./drive_logfile -t -f syslog_log ${srcdir}/logfile_syslog.1

check_output "Syslog timestamp interpreted incorrectly for year end?" <<EOF
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Jan 03 09:47:02 2007 -- 000
EOF

gzip -c ${srcdir}/logfile_syslog.1 > logfile_syslog.1.gz

run_test ./drive_logfile -t -f syslog_log logfile_syslog.1.gz

check_output "Syslog timestamp incorrect for gzipped file?" <<EOF
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Jan 03 09:47:02 2007 -- 000
EOF

if test x"$BZIP2_CMD" != x""; then
    $BZIP2_CMD -z -c "${srcdir}/logfile_syslog.1" > logfile_syslog.1.bz2

    touch -t 200711030923 logfile_syslog.1.bz2
    run_test ./drive_logfile -t -f syslog_log logfile_syslog.1.bz2

    check_output "bzip2 file not loaded?" <<EOF
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Jan 03 09:47:02 2007 -- 000
EOF
fi

touch -t 201404061109 ${srcdir}/logfile_tcf.1
run_test ./drive_logfile -t -f tcf_log ${srcdir}/logfile_tcf.1

check_output "TCF timestamp interpreted incorrectly for hour wrap?" <<EOF
Apr 06 09:59:47 2014 -- 191
Apr 06 10:30:11 2014 -- 474
Apr 06 11:01:11 2014 -- 475
EOF

# The TCSH format converts to local time, so we need to specify a TZ
export TZ="UTC"
run_test ./drive_logfile -t -f tcsh_history ${srcdir}/logfile_tcsh_history.0

check_output "TCSH timestamp interpreted incorrectly?" <<EOF
Nov 02 17:59:26 2006 -- 000
Nov 02 17:59:26 2006 -- 000
Nov 02 17:59:45 2006 -- 000
Nov 02 17:59:45 2006 -- 000
EOF

run_test ./drive_logfile -t -f access_log ${srcdir}/logfile_access_log.0

check_output "access_log timestamp interpreted incorrectly?" <<EOF
Jul 20 22:59:26 2009 -- 000
Jul 20 22:59:29 2009 -- 000
Jul 20 22:59:29 2009 -- 000
EOF

touch -t 200711030923 ${srcdir}/logfile_strace_log.0
run_test ./drive_logfile -t -f strace_log ${srcdir}/logfile_strace_log.0

check_output "strace_log timestamp interpreted incorrectly?" <<EOF
Nov 03 08:09:33 2007 -- 814
Nov 03 08:09:33 2007 -- 815
Nov 03 08:09:33 2007 -- 815
Nov 03 08:09:33 2007 -- 815
Nov 03 08:09:33 2007 -- 816
Nov 03 08:09:33 2007 -- 816
Nov 03 08:09:33 2007 -- 816
Nov 03 08:09:33 2007 -- 816
Nov 03 08:09:33 2007 -- 816
EOF

##

run_test ./drive_logfile -v -f syslog_log ${srcdir}/logfile_syslog.0

check_output "Syslog level interpreted incorrectly?" <<EOF
0x05
0x03
0x05
0x03
EOF

run_test ./drive_logfile -v -f tcsh_history ${srcdir}/logfile_tcsh_history.0

check_output "TCSH level interpreted incorrectly?" <<EOF
0x03
0x83
0x03
0x83
EOF

run_test ./drive_logfile -v -f access_log ${srcdir}/logfile_access_log.0

check_output "access_log level interpreted incorrectly?" <<EOF
0x03
0x05
0x03
EOF

run_test ./drive_logfile -v -f strace_log ${srcdir}/logfile_strace_log.0

check_output "strace_log level interpreted incorrectly?" <<EOF
0x03
0x03
0x03
0x05
0x03
0x05
0x03
0x03
0x03
EOF

run_test ./drive_logfile -t -f generic_log ${srcdir}/logfile_generic.0

check_output "generic_log timestamp interpreted incorrectly?" <<EOF
Jul 02 10:22:40 2012 -- 672
EOF

run_test ./drive_logfile -v -f generic_log ${srcdir}/logfile_generic.0

check_output "generic_log level interpreted incorrectly?" <<EOF
0x02
EOF

touch -t 200711030923 ${srcdir}/logfile_glog.0
run_test ./drive_logfile -t -f glog_log ${srcdir}/logfile_glog.0

check_output "glog_log timestamp interpreted incorrectly?" <<EOF
May 17 15:04:22 2007 -- 619
May 17 15:04:22 2007 -- 619
May 17 15:04:22 2007 -- 619
May 17 15:04:22 2007 -- 619
May 17 15:04:22 2007 -- 619
May 17 15:04:22 2007 -- 619
May 17 15:04:22 2007 -- 619
EOF

run_test ./drive_logfile -v -f glog_log ${srcdir}/logfile_glog.0

check_output "glog_log level interpreted incorrectly?" <<EOF
0x05
0x03
0x03
0x04
0x03
0x03
0x05
EOF

cp ${srcdir}/logfile_syslog.0 truncfile.0

run_test ${lnav_test} -n \
    -c ";update syslog_log set log_mark = 1 where log_line = 1" \
    -c ":write-to truncfile.0" \
    -c ":goto 1" \
    truncfile.0

check_output "truncated log file not detected" <<EOF
Nov  3 09:23:38 veridian automount[16442]: attempting to mount entry /auto/opt
EOF
