#! /bin/bash

touch unreadable.log
chmod ugo-r unreadable.log

run_test ${lnav_test} -n unreadable.log

sed -i "" -e "s|/.*/unreadable.log|unreadable.log|g" `test_err_filename`

check_error_output "able to read an unreadable log file?" <<EOF
error: Permission denied -- 'unreadable.log'
EOF

run_test ${lnav_test} -n 'unreadable.*'

check_output "unreadable file was not skipped" <<EOF
EOF

run_test ./drive_logfile -f syslog_log ${srcdir}/logfile_syslog.0

on_error_fail_with "Didn't infer syslog log format?"

run_test ./drive_logfile -f tcsh_history ${srcdir}/logfile_tcsh_history.0

on_error_fail_with "Didn't infer tcsh-history log format?"

run_test ./drive_logfile -f access_log ${srcdir}/logfile_access_log.0

on_error_fail_with "Didn't infer access_log log format?"

run_test ./drive_logfile -f strace_log ${srcdir}/logfile_strace_log.0

on_error_fail_with "Didn't infer strace_log log format?"

run_test ./drive_logfile -f zblued_log ${srcdir}/logfile_blued.0

on_error_fail_with "Didn't infer blued_log that collides with syslog?"


run_test ./drive_logfile ${srcdir}/logfile_empty.0

on_error_fail_with "Didn't handle empty log?"

cp ${srcdir}/logfile_syslog.0 logfile_syslog.0
touch -t 200711030923 logfile_syslog.0
run_test ./drive_logfile -t -f syslog_log logfile_syslog.0

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

if [ "$BZIP2_SUPPORT"  -eq 1 ] && [ x"$BZIP2_CMD" != x"" ] ; then
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


run_test ./drive_logfile -t -f epoch_log ${srcdir}/logfile_epoch.0

check_output "epoch_log timestamp interpreted incorrectly?" <<EOF
Apr 10 02:58:07 2015 -- 123
Apr 10 02:58:07 2015 -- 456
EOF


touch -t 201509130923 ${srcdir}/logfile_syslog_with_mixed_times.0
run_test ./drive_logfile -t -f syslog_log ${srcdir}/logfile_syslog_with_mixed_times.0

check_output "syslog_log with mixed times interpreted incorrectly?" <<EOF
Sep 13 00:58:45 2015 -- 000
Sep 13 00:59:30 2015 -- 000
Sep 13 01:23:54 2015 -- 000
Sep 13 03:12:04 2015 -- 000
Sep 13 03:12:04 2015 -- 000
Sep 13 03:12:04 2015 -- 000
Sep 13 03:12:04 2015 -- 000
Sep 13 03:12:58 2015 -- 000
Sep 13 03:46:03 2015 -- 000
Sep 13 03:46:03 2015 -- 000
Sep 13 03:46:03 2015 -- 000
Sep 13 03:46:03 2015 -- 000
Sep 13 03:46:03 2015 -- 000
EOF


##

run_test ./drive_logfile -v -f syslog_log ${srcdir}/logfile_syslog.0

check_output "Syslog level interpreted incorrectly?" <<EOF
0x0a
0x07
0x0a
0x07
EOF

run_test ./drive_logfile -v -f tcsh_history ${srcdir}/logfile_tcsh_history.0

check_output "TCSH level interpreted incorrectly?" <<EOF
0x07
0x87
0x07
0x87
EOF

run_test ./drive_logfile -v -f access_log ${srcdir}/logfile_access_log.0

check_output "access_log level interpreted incorrectly?" <<EOF
0x07
0x0a
0x07
EOF

run_test ./drive_logfile -v -f strace_log ${srcdir}/logfile_strace_log.0

check_output "strace_log level interpreted incorrectly?" <<EOF
0x07
0x07
0x07
0x0a
0x07
0x0a
0x07
0x07
0x07
EOF

run_test ./drive_logfile -t -f generic_log ${srcdir}/logfile_generic.0

check_output "generic_log timestamp interpreted incorrectly?" <<EOF
Jul 02 10:22:40 2012 -- 672
Oct 08 16:56:38 2014 -- 344
EOF

run_test ./drive_logfile -v -f generic_log ${srcdir}/logfile_generic.0

check_output "generic_log level interpreted incorrectly?" <<EOF
0x06
0x09
EOF

run_test ./drive_logfile -v -f generic_log ${srcdir}/logfile_generic.1

check_output "generic_log (1) level interpreted incorrectly?" <<EOF
0x07
0x0a
EOF

run_test ./drive_logfile -v -f generic_log ${srcdir}/logfile_generic.2

check_output "generic_log (2) level interpreted incorrectly?" <<EOF
0x0a
0x0a
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
0x0a
0x07
0x07
0x09
0x07
0x07
0x0a
EOF

cp ${srcdir}/logfile_syslog.0 truncfile.0
chmod u+w truncfile.0

run_test ${lnav_test} -n \
    -c ";update syslog_log set log_mark = 1 where log_line = 1" \
    -c ":write-to truncfile.0" \
    -c ":goto 1" \
    truncfile.0

check_output "truncated log file not detected" <<EOF
Nov  3 09:23:38 veridian automount[16442]: attempting to mount entry /auto/opt
EOF


echo "Hi" | run_test ${lnav_test} -d /tmp/lnav.err -nt -w logfile_stdin.log

check_output "piping to stdin is not working?" <<EOF
2013-06-06T19:13:20.123  Hi
2013-06-06T19:13:20.123  ---- END-OF-STDIN ----
EOF

run_test ${lnav_test} -C ${srcdir}/logfile_bad_syslog.0

sed -i "" -e "s|/.*/logfile_bad_syslog.0|logfile_bad_syslog.0|g" `test_err_filename`

check_error_output "bad syslog line not found?" <<EOF
error:logfile_bad_syslog.0:2:line did not match format syslog_log/regex/std/pattern
error:logfile_bad_syslog.0:2:         line -- Nov  3 09:23:38 veridian lookup for opt failed
error:logfile_bad_syslog.0:2:partial match -- Nov  3 09:23:38 veridian lookup for opt failed
EOF

run_test ${lnav_test} -C ${srcdir}/logfile_bad_access_log.0

sed -i "" -e "s|/.*/logfile_bad_access_log.0|logfile_bad_access_log.0|g" `test_err_filename`

check_error_output "bad access_log line not found?" <<EOF
error:logfile_bad_access_log.0:1:line did not match format access_log/regex/std/pattern
error:logfile_bad_access_log.0:1:         line -- 192.168.202.254 [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
error:logfile_bad_access_log.0:1:partial match -- 192.168.202.254
EOF
