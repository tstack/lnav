#! /bin/bash

export TZ=UTC
echo ${top_srcdir}
echo ${top_builddir}

printf '#Date:\t20\x800-2-02\n0\n' | run_cap_test \
    env TEST_COMMENT="short timestamp" ${lnav_test} -n

printf '000\n000\n#Fields: 0\n0\n#Fields: 0\n0' | run_cap_test \
    env TEST_COMMENT="invalid w3c log" ${lnav_test} -n

printf '#Date:\t3/9/3/0\x85 2\n0\n' | run_cap_test \
    env TEST_COMMENT="invalid w3c timestamp" ${lnav_test} -n

cat > rollover_in.0 <<EOF
2600/2 0 00:00:00 0:
00:2 0 00:00:00 0:
00:2 0 00:00:00 0:
EOF
touch -t 200711030923 rollover_in.0

run_cap_test env TEST_COMMENT="invalid date rollover" ${lnav_test} -n rollover_in.0

printf '#Fields: 0\tcs-bytes\n#Fields: 0\n\t0 #\n0' | run_cap_test \
    env TEST_COMMENT="w3c with dupe #Fields" ${lnav_test} -n

printf '#Fields: \xf9\t)\n0\n' | run_cap_test \
    env TEST_COMMENT="garbage w3c fields #1" ${lnav_test} -n

run_cap_test env TEST_COMMENT="w3c with bad header" ${lnav_test} -n <<EOF
#Fields: 0 time
 00:00
#Date:
EOF

printf '\x2b0\x1b[a' | run_cap_test \
    env TEST_COMMENT="log line with an ansi escape" ${lnav_test} -n

run_cap_test ${lnav_test} -n \
    -c ';SELECT * FROM logline' \
    ${test_dir}/logfile_block.1

if test x"${TSHARK_CMD}" != x""; then
  run_test env TZ=UTC ${lnav_test} -n ${test_dir}/dhcp.pcapng

  check_output "pcap file is not recognized" <<EOF
2004-12-05T19:16:24.317 0.0.0.0 → 255.255.255.255 DHCP 314 DHCP Discover - Transaction ID 0x3d1d
2004-12-05T19:16:24.317 192.168.0.1 → 192.168.0.10 DHCP 342 DHCP Offer    - Transaction ID 0x3d1d
2004-12-05T19:16:24.387 0.0.0.0 → 255.255.255.255 DHCP 314 DHCP Request  - Transaction ID 0x3d1e
2004-12-05T19:16:24.387 192.168.0.1 → 192.168.0.10 DHCP 342 DHCP ACK      - Transaction ID 0x3d1e
EOF

  # make sure piped binary data is left alone
  run_test cat ${test_dir}/dhcp.pcapng | env TZ=UTC ${lnav_test} -n

  check_output "pcap file is not recognized" <<EOF
2004-12-05T19:16:24.317 0.0.0.0 → 255.255.255.255 DHCP 314 DHCP Discover - Transaction ID 0x3d1d
2004-12-05T19:16:24.317 192.168.0.1 → 192.168.0.10 DHCP 342 DHCP Offer    - Transaction ID 0x3d1d
2004-12-05T19:16:24.387 0.0.0.0 → 255.255.255.255 DHCP 314 DHCP Request  - Transaction ID 0x3d1e
2004-12-05T19:16:24.387 192.168.0.1 → 192.168.0.10 DHCP 342 DHCP ACK      - Transaction ID 0x3d1e
EOF

  run_test ${lnav_test} -n ${test_dir}/dhcp-trunc.pcapng

  check_error_output "truncated pcap file is not recognized" <<EOF
✘ error: unable to open file: {test_dir}/dhcp-trunc.pcapng
 reason: tshark: The file "{test_dir}/dhcp-trunc.pcapng" appears to have been cut short in the middle of a packet.
EOF
fi


cp ${srcdir}/logfile_syslog.0 truncfile.0
chmod u+w truncfile.0

run_test ${lnav_test} -d /tmp/lnav.err -n \
    -c ";update syslog_log set log_mark = 1 where log_line = 1" \
    -c ":write-to truncfile.0" \
    -c ":goto 1" \
    truncfile.0

check_output "truncated log file not detected" <<EOF
Nov  3 09:23:38 veridian automount[16442]: attempting to mount entry /auto/opt
EOF


if locale -a | grep fr_FR; then
    cp ${srcdir}/logfile_syslog_fr.0 logfile_syslog_fr_test.0
    touch -t 200711030923 logfile_syslog_fr_test.0
    run_test env LC_ALL=fr_FR.UTF-8 ${lnav_test} -n \
        -c ";SELECT log_time FROM syslog_log" \
        -c ":write-csv-to -" \
        logfile_syslog_fr_test.0

    check_output "french locale is not recognized" <<EOF
log_time
2007-08-19 11:08:37.000
EOF
fi

if test x"${LIBARCHIVE_LIBS}" != x""; then
    rm -rf logfile-tmp
    mkdir logfile-tmp
    run_test env TMPDIR=logfile-tmp ${lnav_test} -n \
      -c ':config /tuning/archive-manager/min-free-space -1' \
      ${srcdir}/logfile_syslog.0

    check_error_output "invalid min-free-space allowed?" <<EOF
✘ error: “-1” is not a valid value for option “/tuning/archive-manager/min-free-space”
 reason: value must be greater than or equal to 0
 --> input:1
 = help: Property Synopsis
           /tuning/archive-manager/min-free-space <bytes>
         Description
           The minimum free space, in bytes, to maintain when unpacking archives
EOF

    rm -rf logfile-tmp/lnav-*
    if test x"${XZ_CMD}" != x""; then
        ${XZ_CMD} -z -c ${srcdir}/logfile_syslog.1 > logfile_syslog.1.xz

        run_test env TMPDIR=logfile-tmp ${lnav_test} -n \
            -c ':config /tuning/archive-manager/min-free-space 1125899906842624' \
            -c ':config /tuning/archive-manager/cache-ttl 1d' \
            ${srcdir}/logfile_syslog.0

        run_test env TMPDIR=logfile-tmp ${lnav_test} -d /tmp/lnav.err -n \
            logfile_syslog.1.xz

        sed -e "s|lnav-user-[0-9]*-work|lnav-user-NNN-work|g" \
            -e "s|arc-[0-9a-z]*-logfile|arc-NNN-logfile|g" \
            -e "s|space on disk \(.*\) is|space on disk (NNN) is|g" \
            -e "s|${builddir}||g" \
            `test_err_filename` > test_logfile.big.out
        mv test_logfile.big.out `test_err_filename`
        check_error_output "decompression worked?" <<EOF
✘ error: unable to open file: /logfile_syslog.1.xz
 reason: available space on disk (NNN) is below the minimum-free threshold (1.0PB).  Unable to unpack 'logfile_syslog.1.xz' to 'logfile-tmp/lnav-user-NNN-work/archives/arc-NNN-logfile_syslog.1.xz'
EOF

        run_test env TMPDIR=logfile-tmp ${lnav_test} -n \
            -c ':config /tuning/archive-manager/min-free-space 33554432' \
            ${srcdir}/logfile_syslog.0

        run_test env TMPDIR=logfile-tmp ${lnav_test} -n \
            logfile_syslog.1.xz

        check_output "decompression not working" <<EOF
Dec  3 09:23:38 veridian automount[7998]: lookup(file): lookup for foobar failed
Dec  3 09:23:38 veridian automount[16442]: attempting to mount entry /auto/opt
Dec  3 09:23:38 veridian automount[7999]: lookup(file): lookup for opt failed
Jan  3 09:47:02 veridian sudo: timstack : TTY=pts/6 ; PWD=/auto/wstimstack/rpms/lbuild/test ; USER=root ; COMMAND=/usr/bin/tail /var/log/messages
EOF
    fi

    tar cfz ${builddir}/test-logs.tgz -C ${top_srcdir} test/logfile_access_log.0 test/logfile_access_log.1 test/logfile_empty.0 -C ${builddir}/.. src/lnav

    dd if=test-logs.tgz of=test-logs-trunc.tgz bs=4096 count=20

    mkdir -p logfile-tmp
    run_test env TMPDIR=logfile-tmp ${lnav_test} \
        -c ':config /tuning/archive-manager/cache-ttl 1d' \
        -n test-logs.tgz

    check_output "archive not unpacked" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
10.112.81.15 - - [15/Feb/2013:06:00:31 +0000] "-" 400 0 "-" "-"
EOF

    if ! test -f logfile-tmp/*/archives/*-test-logs.tgz/test/logfile_access_log.0; then
        echo "archived file not unpacked"
        exit 1
    fi

    if test -w logfile-tmp/*/archives/*-test-logs.tgz/test/logfile_access_log.0; then
        echo "archived file is writable"
        exit 1
    fi

    env TMPDIR=logfile-tmp ${lnav_test} -d /tmp/lnav.err \
        -c ':config /tuning/archive-manager/cache-ttl 0d' \
        -n -q ${srcdir}/logfile_syslog.0

    if test -f logfile-tmp/lnav*/archives/*-test-logs.tgz/test/logfile_access_log.0; then
        echo "archive cache not deleted?"
        exit 1
    fi

    run_test env TMPDIR=logfile-tmp ${lnav_test} -n\
        -c ';SELECT view_name, basename(filepath), visible FROM lnav_view_files' \
        test-logs.tgz

    check_output "archive files not loaded correctly" <<EOF
view_name  basename(filepath)  visible
log       logfile_access_log.0       1
log       logfile_access_log.1       1
EOF

    run_test env TMPDIR=logfile-tmp ${lnav_test} -n \
        test-logs-trunc.tgz

    sed -e "s|${builddir}||g" `test_err_filename` | head -2 \
        > test_logfile.trunc.out
    mv test_logfile.trunc.out `test_err_filename`
    check_error_output "truncated tgz not reported correctly" <<EOF
✘ error: unable to open file: /test-logs-trunc.tgz
 reason: failed to extract 'src/lnav' from archive '/test-logs-trunc.tgz' -- truncated gzip input
EOF

    mkdir -p rotmp
    chmod ugo-w rotmp
    run_test env TMPDIR=rotmp ${lnav_test} -n test-logs.tgz

    sed -e "s|lnav-user-[0-9]*-work|lnav-user-NNN-work|g" \
        -e 's|log\.0 -- .*|log\.0 -- ...|g' \
        -e "s|arc-[0-9a-z]*-test|arc-NNN-test|g" \
        -e "s|${builddir}||g" \
        `test_err_filename` | head -2 \
        > test_logfile.rotmp.out
    cp test_logfile.rotmp.out `test_err_filename`
    check_error_output "archive not unpacked" <<EOF
✘ error: unable to open file: /test-logs.tgz
 reason: unable to create directory: rotmp/lnav-user-NNN-work/archives -- Permission denied
EOF
fi

touch unreadable.log
chmod ugo-r unreadable.log

run_test ${lnav_test} -n unreadable.log

sed -e "s|/.*/unreadable.log|unreadable.log|g" `test_err_filename` | head -3 \
    > test_logfile.unreadable.out

mv test_logfile.unreadable.out `test_err_filename`
check_error_output "able to read an unreadable log file?" <<EOF
✘ error: file exists, but is not readable: unreadable.log
 reason: Permission denied
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

run_test ./drive_logfile -f bro_http_log ${srcdir}/logfile_bro_http.log.0

on_error_fail_with "Didn't infer bro_http_log log format?"

run_test ./drive_logfile -f bro_conn_log ${srcdir}/logfile_bro_conn.log.0

on_error_fail_with "Didn't infer bro_conn_log log format?"

run_test ./drive_logfile -f w3c_log ${srcdir}/logfile_w3c.0

on_error_fail_with "Didn't infer w3c_log log format?"


run_test ./drive_logfile ${srcdir}/logfile_empty.0

on_error_fail_with "Didn't handle empty log?"


run_test ./drive_logfile -t -f w3c_log ${srcdir}/logfile_w3c.2

check_output "w3c timestamp interpreted incorrectly?" <<EOF
Oct 09 16:44:49 2000 -- 000
Oct 09 16:44:49 2000 -- 000
Oct 09 16:48:05 2000 -- 000
Oct 09 16:48:17 2000 -- 000
Oct 09 16:48:24 2000 -- 000
Oct 09 16:48:35 2000 -- 000
Oct 09 16:48:41 2000 -- 000
Oct 09 16:48:41 2000 -- 000
Oct 09 16:48:41 2000 -- 000
Oct 09 16:48:41 2000 -- 000
Oct 09 16:48:44 2000 -- 000
Oct 10 16:44:49 2000 -- 000
Oct 10 16:44:49 2000 -- 000
Oct 10 16:48:05 2000 -- 000
EOF

run_test ./drive_logfile -t -f w3c_log ${srcdir}/logfile_w3c.4

check_output "quoted w3c timestamp interpreted incorrectly?" <<EOF
Jun 28 07:26:35 2017 -- 000
Jun 26 18:21:17 2017 -- 000
EOF

cp ${srcdir}/logfile_syslog.0 logfile_syslog_test.0
touch -t 200711030923 logfile_syslog_test.0
run_test ./drive_logfile -t -f syslog_log logfile_syslog_test.0

check_output "Syslog timestamp interpreted incorrectly?" <<EOF
Nov 03 09:23:38 2007 -- 000
Nov 03 09:23:38 2007 -- 000
Nov 03 09:23:38 2007 -- 000
Nov 03 09:47:02 2007 -- 000
EOF

env TZ=UTC touch -t 200711030923 ${srcdir}/logfile_syslog.1
run_test ./drive_logfile -t -f syslog_log ${srcdir}/logfile_syslog.1

check_output "Syslog timestamp interpreted incorrectly for year end?" <<EOF
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Dec 03 09:23:38 2006 -- 000
Jan 03 09:47:02 2007 -- 000
EOF

touch -t 200711030000 ${srcdir}/logfile_rollover.0
run_test ./drive_logfile -t -f generic_log ${srcdir}/logfile_rollover.0

check_output "Generic timestamp interpreted incorrectly for day rollover?" <<EOF
Nov 02 00:00:00 2007 -- 000
Nov 02 01:00:00 2007 -- 000
Nov 02 02:00:00 2007 -- 000
Nov 02 03:00:00 2007 -- 000
Nov 03 00:00:00 2007 -- 000
Nov 03 00:01:00 2007 -- 000
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

run_test ${lnav_test} -n ${srcdir}/logfile_tcf.1

check_output "timestamps with no dates are not rewritten?" <<EOF
TCF 2014-04-06 09:59:47.191234: Server-Properties: {"Name":"TCF Protocol Logger","OSName":"Linux 3.2.0-60-generic","UserName":"xavier","AgentID":"1fde3dd1-d4be-4f79-8090-6f8d212f03bf","TransportName":"TCP","Proxy":"","ValueAdd":"1","Port":"1534"}
TCF 2014-04-06 10:30:11.474442: 0: ---> C 2 RunControl getChildren null <eom>
TCF 2014-04-06 11:01:11.475557: 0: <--- R 2  ["P1"] <eom>
EOF


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

run_cap_test env TZ=America/Los_Angeles ./drive_logfile -t -f access_log ${srcdir}/logfile_access_log.0

run_cap_test env TZ=America/Los_Angeles ${lnav_test} -n ${srcdir}/logfile_access_log.0

run_test ./drive_logfile -t -f generic_log ${srcdir}/logfile_tai64n.0

check_output "tai64n timestamps interpreted incorrectly?" <<EOF
Sep 22 03:31:05 2005 -- 997
Sep 22 03:31:05 2005 -- 997
Sep 22 03:31:06 2005 -- 210
Sep 22 03:31:06 2005 -- 210
Sep 22 03:31:07 2005 -- 714
Sep 22 03:31:07 2005 -- 714
Sep 22 03:31:07 2005 -- 715
Sep 22 03:31:07 2005 -- 715
Sep 22 03:31:07 2005 -- 954
Sep 22 03:31:07 2005 -- 954
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


run_test ./drive_logfile -t -f epoch_log ${srcdir}/logfile_epoch.1

check_error_output "epoch" <<EOF
EOF

check_output "epoch_log timestamp interpreted incorrectly?" <<EOF
Apr 09 19:58:07 2015 -- 123
Apr 09 19:58:07 2015 -- 456
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
error 0x0
info 0x0
error 0x0
info 0x0
EOF

run_test ./drive_logfile -v -f tcsh_history ${srcdir}/logfile_tcsh_history.0

check_output "TCSH level interpreted incorrectly?" <<EOF
info 0x0
info 0x80
info 0x0
info 0x80
EOF

run_test ./drive_logfile -v -f access_log ${srcdir}/logfile_access_log.0

check_output "access_log level interpreted incorrectly?" <<EOF
info 0x0
error 0x0
info 0x0
EOF

run_test ./drive_logfile -v -f strace_log ${srcdir}/logfile_strace_log.0

check_output "strace_log level interpreted incorrectly?" <<EOF
info 0x0
info 0x0
info 0x0
error 0x0
info 0x0
error 0x0
info 0x0
info 0x0
info 0x0
EOF

run_test ./drive_logfile -t -f generic_log ${srcdir}/logfile_generic.0

check_output "generic_log timestamp interpreted incorrectly?" <<EOF
Jul 02 10:22:40 2012 -- 672
Oct 08 16:56:38 2014 -- 344
EOF

run_test ./drive_logfile -t -f generic_log ${srcdir}/logfile_generic.3

check_output "generic_log timestamp interpreted incorrectly?" <<EOF
Jul 02 10:22:40 2012 -- 672
Oct 08 16:56:38 2014 -- 344
EOF

run_test ./drive_logfile -v -f generic_log ${srcdir}/logfile_generic.0

check_output "generic_log level interpreted incorrectly?" <<EOF
debug 0x0
warning 0x0
EOF

run_test ./drive_logfile -v -f generic_log ${srcdir}/logfile_generic.1

check_output "generic_log (1) level interpreted incorrectly?" <<EOF
info 0x0
error 0x0
EOF

run_test ./drive_logfile -v -f generic_log ${srcdir}/logfile_generic.2

check_output "generic_log (2) level interpreted incorrectly?" <<EOF
error 0x0
error 0x0
EOF

run_cap_test ./drive_logfile -t -f generic_log ${test_dir}/logfile_with_zones.0

run_cap_test env TZ=America/Los_Angeles ./drive_logfile -t -f generic_log ${test_dir}/logfile_with_zones.0

run_cap_test env TZ=America/New_York ./drive_logfile -t -f generic_log ${test_dir}/logfile_with_zones.0

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
error 0x0
info 0x0
info 0x0
warning 0x0
info 0x0
info 0x0
error 0x0
EOF

run_test ./drive_logfile -t -f logfmt_log ${srcdir}/logfile_logfmt.0

check_output "logfmt_log time interpreted incorrectly?" <<EOF
Sep 15 21:17:10 2021 -- 220
Sep 15 21:17:11 2021 -- 674
Sep 15 21:17:11 2021 -- 678
Sep 15 21:17:11 2021 -- 679
Sep 15 21:18:20 2021 -- 335
EOF

run_test ./drive_logfile -v -f logfmt_log ${srcdir}/logfile_logfmt.0

check_output "logfmt_log level interpreted incorrectly?" <<EOF
error 0x0
warning 0x0
info 0x0
info 0x0
error 0x0
EOF

run_test ${lnav_test} -C ${test_dir}/logfile_bad_access_log.0

sed -ibak -e "s|/.*/logfile_bad_access_log.0|logfile_bad_access_log.0|g" `test_err_filename`

check_error_output "bad access_log line not found?" <<EOF
error:logfile_bad_access_log.0:1:line did not match format /access_log/regex/std
error:logfile_bad_access_log.0:1:         line -- 192.168.202.254 [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
error:logfile_bad_access_log.0:1:partial match -- 192.168.202.254
EOF

run_test ${lnav_test} -n -I ${test_dir} ${srcdir}/logfile_w3c.2

check_output "metadata lines not ignored?" <<EOF
16:44:49 1.1.1.1 [2]USER anonymous 331
16:44:49 1.1.1.1 [2]PASS - 230
16:48:05 1.1.1.1 [2]QUIT - 226
16:48:17 1.1.1.1 [3]USER anonymous 331
16:48:24 1.1.1.1 [3]PASS user@domain.com 230
16:48:35 1.1.1.1 [3]sent /user/test.c 226
16:48:41 1.1.1.1 [3]created readme.txt 226
16:48:41 1.1.1.1 [3]created fileid.diz 226
16:48:41 1.1.1.1 [3]created names.dll 226
16:48:41 1.1.1.1 [3]created TEST.EXE 226
16:48:44 1.1.1.1 [3]QUIT - 226
16:44:49 1.1.1.1 [2]USER anonymous 331
16:44:49 1.1.1.1 [2]PASS - 230
16:48:05 1.1.1.1 [2]QUIT - 226
EOF

run_test ${lnav_test} -n -I ${test_dir} ${srcdir}/logfile_w3c.6

check_output "unicode in w3c not working?" <<EOF
2015-01-13 00:32:17 100.79.192.81 GET /robots.txt - 80 - 157.55.39.146 ÄÖÜäöü\ßßßMözillä/5.0+(compatible;+bingbot/2.0;++http://www.bing.com/bingbot.htm) - 404 0 2 1405 242 283
EOF

run_test ${lnav_test} -n -I ${test_dir} ${srcdir}/logfile_epoch.0

check_output "rewriting machine-oriented timestamp didn't work?" <<EOF
2015-04-10 02:58:07.123 Hello, World!
2015-04-10 02:58:07.456 Goodbye, World!
EOF

run_test ${lnav_test} -n -I ${test_dir} ${srcdir}/logfile_crlf.0

check_output "CR-LF line-endings not handled?" <<EOF
2012-07-02 10:22:40,672:DEBUG:foo bar baz
2014-10-08 16:56:38,344:WARN:foo bar baz
EOF

run_test ${lnav_test} -n -I ${test_dir} \
    -c ';SELECT count(*) FROM haproxy_log' \
    ${srcdir}/logfile_haproxy.0

check_output "multi-pattern logs don't work?" <<EOF
count(*)
      17
EOF

run_test ${lnav_test} -n \
    ${srcdir}/logfile_syslog_with_header.0

check_output "multi-pattern logs don't work?" <<EOF
Header1: abc
Header2: def
Nov  3 09:23:38 veridian automount[7998]: lookup(file): lookup for foobar failed
Nov  3 09:23:38 veridian automount[16442]: attempting to mount entry /auto/opt
Nov  3 09:23:38 veridian automount[7999]: lookup(file): lookup for opt failed
Nov  3 09:47:02 veridian sudo: timstack : TTY=pts/6 ; PWD=/auto/wstimstack/rpms/lbuild/test ; USER=root ; COMMAND=/usr/bin/tail /var/log/messages
EOF

run_test ${lnav_test} -n \
    ${srcdir}/logfile_generic_with_header.0

check_output "multi-pattern logs don't work?" <<EOF
Header1: abc
Header2: def
2012-07-02 10:22:40,672:DEBUG:foo bar baz
2014-10-08 16:56:38,344:WARN:foo bar baz
EOF

# XXX get this working...
# run_test ${lnav_test} -n -I ${test_dir} <(cat ${srcdir}/logfile_access_log.0)
#
# check_output "opening a FIFO didn't work?" <<EOF
# 192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
# 192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
# 192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
# EOF

export YES_COLOR=1

touch -t 202211030923 ${test_dir}/logfile_ansi.1

run_cap_test ${lnav_test} -n \
    -c ';SELECT log_time, log_body FROM syslog_log' \
    ${test_dir}/logfile_ansi.1

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view pretty' \
    ${test_dir}/logfile_ansi.1

run_cap_test ${lnav_test} -n \
    -c ';SELECT basename(filepath),descriptor,mimetype,content FROM lnav_file_metadata' \
    logfile_syslog.1.gz

run_cap_test ${lnav_test} -n \
    -c ':filter-in Air Mob' \
    ${test_dir}/logfile_ansi.1

export HOME="./file-tz"
rm -rf "./file-tz"
mkdir -p $HOME

touch -t 200711030000 ${srcdir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ':set-file-timezone America/Los_Angeles' \
    ${test_dir}/logfile_syslog.0

# make sure the file options were saved and restored
run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT options_path, options FROM lnav_file" \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ':set-file-timezone America/New_York' \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ':set-file-timezone bad' \
    ${test_dir}/logfile_syslog.0
