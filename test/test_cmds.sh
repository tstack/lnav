#! /bin/bash

run_test ${lnav_test} -n \
    -c "|${test_dir}/toplevel.lnav 123 456" \
    ${test_dir}/logfile_access_log.0

check_error_output "include toplevel.lnav" <<EOF
EOF

check_output "include toplevel.lnav" <<EOF
toplevel here 123 456
nested here nested.lnav abc
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -f "nonexistent.lnav" \
    ${test_dir}/logfile_access_log.0

check_error_output "include nonexistent" <<EOF
invalid command file: No such file or directory
EOF


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
    -c ":goto 0" \
    -c ":goto 2 hours later" \
    ${test_dir}/logfile_syslog_with_mixed_times.0

check_output "goto 3:45 is not working?" <<EOF
Sep 13 03:12:04 Tim-Stacks-iMac kernel[0]: vm_compressor_record_warmup (9478314 - 9492476)
Sep 13 03:12:04 Tim-Stacks-iMac kernel[0]: AppleBCM5701Ethernet [en0]:        0        0 memWrInd fBJP_Wakeup_Timer
Sep 13 01:25:39 Tim-Stacks-iMac kernel[0]: AppleThunderboltNHIType2::waitForOk2Go2Sx - retries = 60000
Sep 13 03:12:04 Tim-Stacks-iMac kernel[0]: hibernate_page_list_setall(preflight 0) start 0xffffff8428276000, 0xffffff8428336000
Sep 13 03:12:58 Tim-Stacks-iMac kernel[0]: *** kernel exceeded 500 log message per second limit  -  remaining messages this second discarded ***
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: IOThunderboltSwitch<0xffffff803f4b3000>(0x0)::listenerCallback - Thunderbolt HPD packet for route = 0x0 port = 11 unplug = 0
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: vm_compressor_flush - starting
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: AppleBCM5701Ethernet [en0]:        0        0 memWrInd fBJP_Wakeup_Timer
Sep 13 03:13:16 Tim-Stacks-iMac kernel[0]: AppleThunderboltNHIType2::waitForOk2Go2Sx - retries = 60000
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: hibernate_page_list_setall(preflight 0) start 0xffffff838f1fc000, 0xffffff838f2bc000
EOF


run_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":goto 3:45" \
    ${test_dir}/logfile_syslog_with_mixed_times.0

check_output "goto 3:45 is not working?" <<EOF
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: IOThunderboltSwitch<0xffffff803f4b3000>(0x0)::listenerCallback - Thunderbolt HPD packet for route = 0x0 port = 11 unplug = 0
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: vm_compressor_flush - starting
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: AppleBCM5701Ethernet [en0]:        0        0 memWrInd fBJP_Wakeup_Timer
Sep 13 03:13:16 Tim-Stacks-iMac kernel[0]: AppleThunderboltNHIType2::waitForOk2Go2Sx - retries = 60000
Sep 13 03:46:03 Tim-Stacks-iMac kernel[0]: hibernate_page_list_setall(preflight 0) start 0xffffff838f1fc000, 0xffffff838f2bc000
EOF


run_test ${lnav_test} -n \
    -c ":goto invalid" \
    ${test_dir}/logfile_access_log.0

check_error_output "goto invalid is working" <<EOF
error: expecting line number/percentage, timestamp, or relative time
EOF

check_output "goto invalid is not working" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":relative-goto -1" \
    ${test_dir}/logfile_access_log.0

check_output "relative-goto -1 is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":next-mark error" \
    ${test_dir}/logfile_access_log.0

check_output "next-mark error is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":goto -1" \
    -c ":prev-mark error" \
    ${test_dir}/logfile_access_log.0

check_output "prev-mark error is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":next-mark foobar" \
    ${test_dir}/logfile_access_log.0

check_error_output "goto invalid is not working" <<EOF
error: unknown bookmark type
EOF

check_output "invalid mark-type is working" <<EOF
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

cp ${test_dir}/logfile_multiline.0 logfile_append.0
chmod ug+w logfile_append.0

run_test ${lnav_test} -n \
    -c ";update generic_log set log_mark=1" \
    -c ":filter-in Goodbye" \
    -c ":append-to logfile_append.0" \
    -c ":rebuild" \
    logfile_append.0

check_output "filter-in append is not working" <<EOF
2009-07-20 22:59:30,221:ERROR:Goodbye, World!
2009-07-20 22:59:30,221:ERROR:Goodbye, World!
EOF

cp ${test_dir}/logfile_multiline.0 logfile_append.0
chmod ug+w logfile_append.0

run_test ${lnav_test} -n \
    -c ":filter-out Goodbye" \
    -c ":shexec echo '2009-07-20 22:59:30,221:ERROR:Goodbye, World!' >> logfile_append.0" \
    -c ":rebuild" \
    logfile_append.0

check_output "filter-out append is not working" <<EOF
2009-07-20 22:59:27,672:DEBUG:Hello, World!
  How are you today?
EOF


run_test ${lnav_test} -n \
    -c ":filter-in avahi" \
    -c ":delete-filter avahi" \
    -c ":filter-in avahi" \
    -c ":filter-in dnsmasq" \
    ${test_dir}/logfile_filter.0

check_output "multiple filter-in is not working" <<EOF
Dec  6 13:01:34 ubu-mac avahi-daemon[786]: Joining mDNS multicast group on interface virbr0.IPv4 with address 192.168.122.1.
Dec  6 13:01:34 ubu-mac avahi-daemon[786]: New relevant interface virbr0.IPv4 for mDNS.
Dec  6 13:01:34 ubu-mac avahi-daemon[786]: Registering new address record for 192.168.122.1 on virbr0.IPv4.
Dec  6 13:01:34 ubu-mac dnsmasq[1840]: started, version 2.68 cachesize 150
Dec  6 13:01:34 ubu-mac dnsmasq[1840]: compile time options: IPv6 GNU-getopt DBus i18n IDN DHCP DHCPv6 no-Lua TFTP conntrack ipset auth
Dec  6 13:01:34 ubu-mac dnsmasq-dhcp[1840]: DHCP, IP range 192.168.122.2 -- 192.168.122.254, lease time 1h
Dec  6 13:01:34 ubu-mac dnsmasq-dhcp[1840]: DHCP, sockets bound exclusively to interface virbr0
Dec  6 13:01:34 ubu-mac dnsmasq[1840]: reading /etc/resolv.conf
Dec  6 13:01:34 ubu-mac dnsmasq[1840]: using nameserver 192.168.1.1#53
Dec  6 13:01:34 ubu-mac dnsmasq[1840]: read /etc/hosts - 5 addresses
Dec  6 13:01:34 ubu-mac dnsmasq[1840]: read /var/lib/libvirt/dnsmasq/default.addnhosts - 0 addresses
Dec  6 13:01:34 ubu-mac dnsmasq-dhcp[1840]: read /var/lib/libvirt/dnsmasq/default.hostsfile
EOF


run_test ${lnav_test} -n \
    -c ":switch-to-view text" \
    -c ":filter-in World" \
    ${test_dir}/logfile_plain.0
check_output "plain text filter-in is not working" <<EOF
Hello, World!
Goodbye, World!
EOF

run_test ${lnav_test} -n \
    -c ":switch-to-view text" \
    -c ":filter-out World" \
    ${test_dir}/logfile_plain.0
check_output "plain text filter-out is not working" <<EOF
How are you?
EOF


TOO_MANY_FILTERS=""
for i in `seq 1 33`; do
    TOO_MANY_FILTERS="$TOO_MANY_FILTERS -c ':filter-out $i'"
done
run_test eval ${lnav_test} -d /tmp/lnav.err -n \
    $TOO_MANY_FILTERS \
    ${test_dir}/logfile_filter.0
check_error_output "able to create too many filters?" <<EOF
error: filter limit reached, try combining filters with a pipe symbol (e.g. foo|bar)
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
        "log_part": null,
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
        "log_part": null,
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
        "log_part": null,
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
    -c ";update generic_log set log_mark=1" \
    -c ":pipe-to sed -e 's/World!/Bork!/g'" \
    ${test_dir}/logfile_multiline.0
check_output "pipe-to is not working" <<EOF
2009-07-20 22:59:27,672:DEBUG:Hello, Bork!
  How are you today?
2009-07-20 22:59:27,672:DEBUG:Hello, World!
  How are you today?
2009-07-20 22:59:30,221:ERROR:Goodbye, Bork!
2009-07-20 22:59:30,221:ERROR:Goodbye, World!
EOF

run_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ":pipe-line-to sed -e 's/World!/Bork!/g'" \
    ${test_dir}/logfile_multiline.0
check_output "pipe-line-to is not working" <<EOF
2009-07-20 22:59:27,672:DEBUG:Hello, World!
  How are you today?
2009-07-20 22:59:30,221:ERROR:Goodbye, Bork!
2009-07-20 22:59:30,221:ERROR:Goodbye, World!
EOF

run_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":pipe-line-to echo \$cs_uri_stem \$sc_status" \
    ${test_dir}/logfile_access_log.0
check_output "pipe-line-to env vars are not working" <<EOF
/vmw/cgi/tramp 200
EOF


run_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/textfile_json_one_line.0
check_output "pretty-printer is not working for text files" <<EOF
{
    "foo bar" : null,
    "array" : [
        1,
        2,
        3
    ],
    "obj" : {
        "one" : 1,
        "two" : true
    }
}
EOF

run_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/textfile_json_one_line.0
check_output "pretty-printer is not working for indented text files" <<EOF
{
  "foo bar": null,
  "array": [
    1,
    2,
    3
  ],
  "obj": {
    "one": 1,
    "two": true
  }
}
EOF

run_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/textfile_quoted_json.0
check_output "pretty-printer is not working for quoted text" <<EOF
{
  "foo bar": null,
  "array": [
    1,
    2,
    3
  ],
  "obj": {
    "one": 1,
    "two": true
  }
}
EOF

run_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_vami.0
check_output "pretty-printer is not working" <<EOF
2015-03-12T23:16:52.071:INFO:com.root:Response :

<?xml version="1.0"?>
<response>
    <locale>en-US</locale>
    <requestid>ipInfo</requestid>
    <value id="ipv4Gateway" actions="enabled">198.51.100.253 (unknown)</value>
    <value id="ipv6Gateway" actions="enabled"/>
    <value id="ipv6Enabled" actions="enabled">true</value>
    <value id="ipv4Enabled" actions="enabled">true</value>
    <value id="name" actions="enabled">nic1</value>
    <value id="v4config" actions="enabled">
        <value id="defaultGateway" actions="enabled">0.0.0.0 (unknown)</value>
        <value id="updateable" actions="enabled">True</value>
        <value id="prefix" actions="enabled">22</value>
        <value id="mode" actions="enabled">dhcp</value>
        <value id="address" actions="enabled">198.51.100.110 (unknown)</value>
        <value id="interface" actions="enabled">nic1</value>
    </value>
    <value id="v6config" actions="enabled">
        <value id="defaultGateway" actions="enabled">fe80::214:f609:19f7:6bf1 (unknown)</value>
        <value id="updateable" actions="enabled">True</value>
        <value id="interface" actions="enabled">nic1</value>
        <value id="dhcp" actions="enabled">False</value>
        <value id="autoconf" actions="enabled">False</value>
        <value id="addresses" actions="enabled">
            <value id="origin" actions="enabled">other</value>
            <value id="status" actions="enabled">preferred</value>
            <value id="prefix" actions="enabled">64</value>
            <value id="address" actions="enabled">fe80::250:56ff:feaa:5abf (unknown)</value>
        </value>
    </value>
    <value id="interfaceInfo" actions="enabled">
        <value id="status" actions="enabled">up</value>
        <value id="mac" actions="enabled">00:50:56:aa:5a:bf</value>
        <value id="name" actions="enabled">nic1</value>
    </value>
</response>
EOF


run_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_pretty.0
check_output "pretty-printer is not working" <<EOF
Apr  7 00:49:42 Tim-Stacks-iMac kernel[0]: Ethernet [AppleBCM5701Ethernet]: Link up on en0, 1-Gigabit, Full-duplex, Symmetric flow-control, Debug [
    796d,
    2301,
    0de1,
    0300,
    cde1,
    3800
]
Apr  7 05:49:53 Tim-Stacks-iMac.local GoogleSoftwareUpdateDaemon[17212]: -[KSUpdateCheckAction performAction]
 KSUpdateCheckAction running KSServerUpdateRequest:
<KSOmahaServerUpdateRequest:0x511f30
		server=<KSOmahaServer:0x510d80>
    url="https://tools.google.com/service/update2"
    runningFetchers=0
    tickets=1
    activeTickets=1
    rollCallTickets=1
    body=

    <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
    <o:gupdate xmlns:o="http://www.google.com/update2/request" protocol="2.0" version="KeystoneDaemon-1.2.0.7709" ismachine="1" requestid="{0DFDBCD1-5E29-4DFC-BD99-31A2397198FE}">
        <o:os platform="mac" version="MacOSX" sp="10.10.2_x86_64h"></o:os>
        <o:app appid="com.google.Keystone" version="1.2.0.7709" lang="en-us" installage="180" brand="GGLG">
            <o:ping r="1" a="1"></o:ping>
            <o:updatecheck></o:updatecheck>
        </o:app>
    </o:gupdate>
    >
Apr  7 07:31:56 Tim-Stacks-iMac.local VirtualBox[36403]: WARNING: The Gestalt selector gestaltSystemVersion is returning 10.9.2 instead of 10.10.2. Use NSProcessInfo's operatingSystemVersion property to get correct system version number.
	Call location:
Apr  7 07:31:56 Tim-Stacks-iMac.local VirtualBox[36403]: 0   CarbonCore                          0x00007fff8a9b3d9b ___Gestalt_SystemVersion_block_invoke + 113
Apr  7 07:31:56 Tim-Stacks-iMac.local VirtualBox[36403]: 1   libdispatch.dylib                   0x00007fff8bc84c13 _dispatch_client_callout + 8
Apr  7 07:32:56 Tim-Stacks-iMac.local logger[234]: Bad data {
    abc,
    123,
    456
)
}]
EOF


run_test ${lnav_test} -n \
    -c ":set-min-log-level error" \
    ${test_dir}/logfile_access_log.0

check_output "set-min-log-level is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":highlight foobar" \
    -c ":clear-highlight foobar" \
    ${test_dir}/logfile_access_log.0

check_error_output "clear-highlight is not working?" <<EOF
EOF

run_test ${lnav_test} -n \
    -c ":clear-highlight foobar" \
    ${test_dir}/logfile_access_log.0

check_error_output "clear-highlight did not report an error?" <<EOF
error: highlight does not exist
EOF

touch -t 200711030923 ${srcdir}/logfile_syslog.0
run_test ${lnav_test} -n \
    -c ":switch-to-view histogram" \
    -c ":zoom-to 4-hour" \
    ${test_dir}/logfile_syslog.0

check_output "histogram is not working?" <<EOF
 Sat Nov 03 08:00          2 normal         2 errors         0 warnings         0 marks
EOF

run_test ${lnav_test} -n \
    -c ":switch-to-view histogram" \
    -c ":zoom-to day" \
    ${test_dir}/logfile_syslog.0

check_output "histogram is not working?" <<EOF
 Sat Nov 03 00:00          2 normal         2 errors         0 warnings         0 marks
EOF

run_test ${lnav_test} -n \
    -c ":filter-in sudo" \
    -c ":switch-to-view histogram" \
    -c ":zoom-to 4-hour" \
    ${test_dir}/logfile_syslog.0

check_output "histogram is not working?" <<EOF
 Sat Nov 03 08:00          1 normal         0 errors         0 warnings         0 marks
EOF

run_test ${lnav_test} -n \
    -c ":zoom-to bad" \
    ${test_dir}/logfile_access_log.0

check_error_output "bad zoom level is not rejected?" <<EOF
error: invalid zoom level -- bad
EOF


run_test ${lnav_test} -n \
    -f ${test_dir}/multiline.lnav \
    ${test_dir}/logfile_access_log.0

check_output "multiline commands do not work?" <<EOF
Hello: Jules
EOF


printf 'Hello, World!' | run_test ${lnav_test} -n \
  -c ":switch-to-view text"

check_output "stdin with no line feed failed" <<EOF
Hello, World!
EOF


run_test ${lnav_test} -n \
    -c ":hide-lines-before 2009-07-20T22:59:29" \
    ${test_dir}/logfile_access_log.0

check_output "hide-lines-before does not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":hide-lines-after 2009-07-20T22:59:26" \
    ${test_dir}/logfile_access_log.0

check_output "hide-lines-after does not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":hide-lines-after 2009-07-20T22:59:26" \
    -c ":show-lines-before-and-after" \
    ${test_dir}/logfile_access_log.0

check_output "hide-lines-after does not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

export XYZ="World"

run_test ${lnav_test} -n \
    -c ':echo Hello, $XYZ!' \
    ${test_dir}/logfile_access_log.0

check_output "echo hello" <<EOF
Hello, \$XYZ!
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

export XYZ="World"

run_test ${lnav_test} -n \
    -c ':echo -n Hello, ' \
    -c ':echo World!' \
    ${test_dir}/logfile_access_log.0

check_output "echo hello" <<EOF
Hello, World!
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ':eval :echo Hello, $XYZ!' \
    ${test_dir}/logfile_access_log.0

check_output "eval echo hello" <<EOF
Hello, World!
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF
