#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -n \
    -c ";.read nonexistent-file" \
    ${test_dir}/logfile_empty.0

check_error_output "read worked with a nonexistent file?" <<EOF
command-option:1: error: unable to read script file: nonexistent-file -- No such file or directory
EOF

run_test ${lnav_test} -n \
    -c ";.read ${test_dir}/file_for_dot_read.sql" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output ".read did not work?" <<EOF
log_line,log_body
1, attempting to mount entry /auto/opt
EOF


run_test ${lnav_test} -n \
    -c ";SELECT replicate('foobar', 120)" \
    ${test_dir}/logfile_empty.0

check_output "long lines are not truncated?" <<EOF
                                                replicate('foobar', 120)
foobarfoobarfoobarfoobarfoobarfoobarfoobarfoobarfoobarfoobar⋯oobarfoobarfoobarfoobarfoobarfoobarfoobarfoobarfoobarfoobar
EOF

cp ${srcdir}/logfile_syslog.2 logfile_syslog_test.2
touch -t 201511030923 logfile_syslog_test.2
run_test ${lnav_test} -n \
    -c ";SELECT *, log_msg_schema FROM all_logs" \
    -c ":write-csv-to -" \
    logfile_syslog_test.2

check_output "all_logs does not work?" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,log_format,log_msg_format,log_msg_schema
0,<NULL>,2015-11-03 09:23:38.000,0,info,0,<NULL>,<NULL>,<NULL>,syslog_log,# is up,aff2bfc3c61e7b86329b83190f0912b3
1,<NULL>,2015-11-03 09:23:38.000,0,info,0,<NULL>,<NULL>,<NULL>,syslog_log,# is up,aff2bfc3c61e7b86329b83190f0912b3
2,<NULL>,2015-11-03 09:23:38.000,0,info,0,<NULL>,<NULL>,<NULL>,syslog_log,# is down,506560b3c73dee057732e69a3c666718
EOF


run_test ${lnav_test} -n \
    -c ";SELECT sc_substatus FROM w3c_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_w3c.3

check_output "w3c quoted strings are not handled correctly?" <<EOF
[
    {
        "sc_substatus": 0
    },
    {
        "sc_substatus": 0
    },
    {
        "sc_substatus": null
    }
]
EOF

run_test ${lnav_test} -n \
    -c ";SELECT cs_headers FROM w3c_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_w3c.3

check_output "w3c headers are not captured?" <<EOF
[
    {
        "cs_headers": {
            "User-Agent": "Mozilla/5.0 (Linux; Android 4.4.4; SM-G900V Build/KTU84P) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/39.0.2171.59 Mobile Safari/537.36",
            "Referer": "http://example.com/Search/SearchResults.pg?informationRecipient.languageCode.c=en",
            "Host": "xzy.example.com"
        }
    },
    {
        "cs_headers": {
            "User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2227.1 Safari/537.36",
            "Referer": null,
            "Host": "example.hello.com"
        }
    },
    {
        "cs_headers": {
            "User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/37.0.2062.124 Safari/537.36",
            "Referer": null,
            "Host": "hello.example.com"
        }
    }
]
EOF

run_test ${lnav_test} -n \
    -c ";SELECT * FROM generate_series()" \
    ${test_dir}/logfile_access_log.0

check_error_output "generate_series() works without params?" <<EOF
command-option:1: error: the start parameter is required
EOF

run_test ${lnav_test} -n \
    -c ";SELECT * FROM generate_series(1)" \
    ${test_dir}/logfile_access_log.0

check_error_output "generate_series() works without stop param?" <<EOF
command-option:1: error: the stop parameter is required
EOF

run_test ${lnav_test} -n \
    -c ";SELECT raise_error('oops!')" \
    ${test_dir}/logfile_access_log.0

check_error_output "raise_error() does not work?" <<EOF
command-option:1: error: oops!
EOF

run_test ${lnav_test} -n \
    -c ";SELECT basename(filepath) as name, content, length(content) FROM lnav_file" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_empty.0

check_output "empty content not handled correctly?" <<EOF
name,content,length(content)
logfile_empty.0,,0
EOF

run_test ${lnav_test} -n \
    -c ";SELECT distinct xp.node_text FROM lnav_file, xpath('//author', content) as xp" \
    -c ":write-csv-to -" \
    ${test_dir}/books.xml

check_output "xpath on file content not working?" <<EOF
node_text
"Gambardella, Matthew"
"Ralls, Kim"
"Corets, Eva"
"Randall, Cynthia"
"Thurman, Paula"
"Knorr, Stefan"
"Kress, Peter"
"O'Brien, Tim"
"Galos, Mike"
EOF

gzip -c ${srcdir}/logfile_json.json > logfile_json.json.gz
dd if=logfile_json.json.gz of=logfile_json-trunc.json.gz bs=64 count=2

# TODO re-enable this
#run_test ${lnav_test} -n \
#    -c ";SELECT content FROM lnav_file" \
#    logfile_json-trunc.json.gz

#check_error_output "invalid gzip file working?" <<EOF
#command-option:1: error: unable to uncompress: logfile_json-trunc.json.gz -- buffer error
#EOF

run_test ${lnav_test} -n \
    -c ";SELECT jget(rc.content, '/ts') AS ts FROM lnav_file, regexp_capture(lnav_file.content, '.*\n') as rc" \
    -c ":write-csv-to -" \
    logfile_json.json.gz

check_output "jget on file content not working?" <<EOF
ts
2013-09-06T20:00:48.124817Z
2013-09-06T20:00:49.124817Z
2013-09-06T22:00:49.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:00:59.124817Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
2013-09-06T22:01:49.124817Z
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_file SET visible=0" \
    ${test_dir}/logfile_access_log.0

check_output "file is not hidden?" <<EOF
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_file SET filepath='foo' WHERE endswith(filepath, '_log.0')" \
    ${test_dir}/logfile_access_log.0

check_error_output "able to change a real file's path?" <<EOF
command-option:1: error: real file paths cannot be updated, only symbolic ones
EOF

run_test ${lnav_test} -n \
    -c "|rename-stdin" \
    ${test_dir}/logfile_access_log.0 < /dev/null

check_error_output "rename-stdin works without an argument?" <<EOF
../test/.lnav/formats/default/rename-stdin.lnav:6: error: expecting the new name for stdin as the first argument
EOF

run_test ${lnav_test} -n \
    -c "|rename-stdin foo" \
    ${test_dir}/logfile_access_log.0 < /dev/null

check_error_output "rename-stdin when there is no stdin file?" <<EOF
../test/.lnav/formats/default/rename-stdin.lnav:7: error: no data was redirected to lnav's standard-input
EOF

echo 'Hello, World!' | run_test ${lnav_test} -n \
    -c "|rename-stdin foo" \
    -c ";SELECT filepath FROM lnav_file"

check_output "rename of stdin did not work?" <<EOF
filepath
foo
EOF

run_test ${lnav_test} -n \
    -c ";SELECT basename(filepath),format,lines,time_offset FROM lnav_file LIMIT 2" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0 \
    ${test_dir}/logfile_access_log.1

check_output "lnav_file table is not working?" <<EOF
basename(filepath),format,lines,time_offset
logfile_access_log.0,access_log,3,0
logfile_access_log.1,access_log,1,0
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_file SET time_offset = 60 * 1000" \
    ${test_dir}/logfile_access_log.0 \
    ${test_dir}/logfile_access_log.1

check_output "time_offset in lnav_file table is not working?" <<EOF
192.168.202.254 - - [20/Jul/2009:23:00:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:23:00:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:23:00:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
10.112.81.15 - - [15/Feb/2013:06:01:31 +0000] "-" 400 0 "-" "-"
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_file SET time_offset=14400000 WHERE endswith(filepath, 'logfile_block.1')" \
    ${test_dir}/logfile_block.1 \
    ${test_dir}/logfile_block.2

check_output "time_offset in lnav_file table is not reordering?" <<EOF
Wed May 19 12:00:01  2021 line 1
Wed May 19 12:00:02 UTC 2021 line 2
Wed May 19 12:00:03  2021 line 3
Wed May 19 12:00:04 UTC 2021 line 4
EOF

run_test ${lnav_test} -n \
    -c ";SELECT view_name,basename(filepath),visible FROM lnav_view_files" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.*

check_output "lnav_view_files does not work?" <<EOF
view_name,basename(filepath),visible
log,logfile_access_log.0,1
log,logfile_access_log.1,1
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_view_files SET visible=0 WHERE endswith(filepath, 'log.0')" \
    ${test_dir}/logfile_access_log.*

check_output "updating lnav_view_files does not work?" <<EOF
10.112.81.15 - - [15/Feb/2013:06:00:31 +0000] "-" 400 0 "-" "-"
EOF

run_test ${lnav_test} -n \
    -c ";DELETE FROM lnav_view_stack" \
    ${test_dir}/logfile_access_log.0

check_output "deleting the view stack does not work?" <<EOF
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_view_stack SET name = 'foo'" \
    ${test_dir}/logfile_access_log.0

check_error_output "able to update lnav_view_stack?" <<EOF
command-option:1: error: The lnav_view_stack table cannot be updated
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_stack VALUES ('help')" \
    -c ";DELETE FROM lnav_view_stack WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

check_error_output "able to delete a view in the middle of lnav_view_stack?" <<EOF
command-option:2: error: Only the top view in the stack can be deleted
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('log', 0, 1, 'out', '')" \
    ${test_dir}/logfile_access_log.0

check_error_output "inserted filter with an empty pattern?" <<EOF
command-option:1: error: Expecting an non-empty pattern for column number 4
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('log', 0, 1, 'out', 'abc(')" \
    ${test_dir}/logfile_access_log.0

check_error_output "inserted filter with an invalid pattern?" <<EOF
command-option:1: error: Invalid regular expression in column 4: missing ) at offset 4
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('bad', 0, 1, 'out', 'abc')" \
    ${test_dir}/logfile_access_log.0

check_error_output "inserted filter with an invalid view name?" <<EOF
command-option:1: error: Expecting an lnav view name for column number 0
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES (NULL, 0, 1, 'out', 'abc')" \
    ${test_dir}/logfile_access_log.0

check_error_output "inserted filter with a null view name?" <<EOF
command-option:1: error: Expecting an lnav view name for column number 0
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('log', 0 , 1, 'bad', 'abc')" \
    ${test_dir}/logfile_access_log.0

check_error_output "inserted filter with an invalid filter type?" <<EOF
command-option:1: error: Expecting an filter type for column number 3
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern) VALUES ('log', 'vmk')" \
    ${test_dir}/logfile_access_log.0

check_output "inserted filter-out did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern, type) VALUES ('log', 'vmk', 'in')" \
    ${test_dir}/logfile_access_log.0

check_output "inserted filter-in did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    -c ";DELETE FROM lnav_view_filters" \
    ${test_dir}/logfile_access_log.0

check_output "inserted filter did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    -c ";UPDATE lnav_view_filters SET pattern = 'vmkboot'" \
    ${test_dir}/logfile_access_log.0

check_output "inserted filter did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    -c ";SELECT * FROM lnav_view_filter_stats" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "view filter stats is not working?" <<EOF
view_name,filter_id,hits
log,1,2
EOF

run_test ${lnav_test} -n \
    -c ";SELECT * FROM access_log LIMIT 0" \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "output generated for empty result set?" <<EOF
EOF

run_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_line()" \
    ${test_dir}/logfile_uwsgi.0

check_output "log_top_line() not working?" <<EOF
log_top_line()
             2
EOF

run_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_line()" \
    ${test_dir}/logfile_empty.0

check_output "log_top_line() for an empty log file is not working?" <<EOF
log_top_line()
        <NULL>
EOF

run_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_datetime()" \
    ${test_dir}/logfile_uwsgi.0

check_output "log_top_datetime() not working?" <<EOF
   log_top_datetime()
2016-03-13 22:49:15.000
EOF

run_test ${lnav_test} -n \
    -c ":goto 2" \
    -c ";SELECT log_top_datetime()" \
    ${test_dir}/logfile_empty.0

check_output "log_top_datetime() for an empty log file is not working?" <<EOF
    log_top_datetime()
            <NULL>
EOF

run_test ${lnav_test} -n \
    -c ";SELECT * FROM uwsgi_log LIMIT 1" \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_uwsgi.0

check_output "uwsgi not working?" <<EOF
log_line log_part         log_time        log_idle_msecs log_level log_mark log_comment log_tags log_filters    c_ip   cs_bytes cs_method cs_uri_query   cs_uri_stem   cs_username cs_vars cs_version s_app s_core s_pid s_req s_runtime s_switches s_worker_reqs sc_bytes sc_header_bytes sc_headers sc_status
       0   <NULL> 2016-03-13 22:49:12.000              0 info             0      <NULL>   <NULL>      <NULL> 127.0.0.1      696 POST            <NULL> /update_metrics                  38 HTTP/1.1   0     3      88185     1     0.129          1             1       47             378          9       200
EOF

run_test ${lnav_test} -n \
    -c ";SELECT s_runtime FROM uwsgi_log LIMIT 5" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_uwsgi.0

check_output "uwsgi scaling not working?" <<EOF
s_runtime
0.129
0.035
6.8e-05
0.016
0.01
EOF

run_test env TZ=UTC ${lnav_test} -n \
    -c ";SELECT bro_conn_log.bro_duration as duration, bro_conn_log.bro_uid, group_concat( distinct (bro_method || ' ' || bro_host)) as req from bro_http_log, bro_conn_log where bro_http_log.bro_uid = bro_conn_log.bro_uid group by bro_http_log.bro_uid order by duration desc limit 10" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_bro_http.log.0 ${test_dir}/logfile_bro_conn.log.0

check_output "bro logs are not recognized?" <<EOF
duration,bro_uid,req
116.438679,CwFs1P2UcUdlSxD2La,GET www.reddit.com
115.202498,CdZUPH2DKOE7zzCLE3,GET feeds.bbci.co.uk
115.121914,CdrfXZ1NOFPEawF218,GET c.thumbs.redditmedia.com
115.121837,CoX7zA3OJKGUOSCBY2,GET e.thumbs.redditmedia.com
115.12181,CJxSUgkInyKSHiju1,GET e.thumbs.redditmedia.com
115.121506,CT0JIh479jXIGt0Po1,GET f.thumbs.redditmedia.com
115.121339,CJwUi9bdB9c1lLW44,GET f.thumbs.redditmedia.com
115.119217,C6Q4Vm14ZJIlZhsXqk,GET a.thumbs.redditmedia.com
72.274459,CbNCgO1MzloHRNeY4f,GET www.google.com
71.658218,CnGze54kQWWpKqrrZ4,GET ajax.googleapis.com
EOF

run_test env TZ=UTC ${lnav_test} -n \
    -c ";SELECT * FROM bro_http_log LIMIT 5" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_bro_http.log.0

check_output "bro logs are not recognized?" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,bro_ts,bro_uid,bro_id_orig_h,bro_id_orig_p,bro_id_resp_h,bro_id_resp_p,bro_trans_depth,bro_method,bro_host,bro_uri,bro_referrer,bro_version,bro_user_agent,bro_request_body_len,bro_response_body_len,bro_status_code,bro_status_msg,bro_info_code,bro_info_msg,bro_tags,bro_username,bro_password,bro_proxied,bro_orig_fuids,bro_orig_filenames,bro_orig_mime_types,bro_resp_fuids,bro_resp_filenames,bro_resp_mime_types
0,<NULL>,2011-11-03 00:19:26.452,0,info,0,<NULL>,<NULL>,<NULL>,1320279566.452687,CwFs1P2UcUdlSxD2La,192.168.2.76,52026,132.235.215.119,80,1,GET,www.reddit.com,/,<NULL>,1.1,Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1,0,109978,200,OK,<NULL>,<NULL>,,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,Ftw3fJ2JJF3ntMTL2,<NULL>,text/html
1,<NULL>,2011-11-03 00:19:26.831,379,info,0,<NULL>,<NULL>,<NULL>,1320279566.831619,CJxSUgkInyKSHiju1,192.168.2.76,52030,72.21.211.173,80,1,GET,e.thumbs.redditmedia.com,/E-pbDbmiBclPkDaX.jpg,http://www.reddit.com/,1.1,Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1,0,2300,200,OK,<NULL>,<NULL>,,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,FFTf9Zdgk3YkfCKo3,<NULL>,image/jpeg
2,<NULL>,2011-11-03 00:19:26.831,0,info,0,<NULL>,<NULL>,<NULL>,1320279566.831563,CJwUi9bdB9c1lLW44,192.168.2.76,52029,72.21.211.173,80,1,GET,f.thumbs.redditmedia.com,/BP5bQfy4o-C7cF6A.jpg,http://www.reddit.com/,1.1,Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1,0,2272,200,OK,<NULL>,<NULL>,,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,FfXtOj3o7aub4vbs2j,<NULL>,image/jpeg
3,<NULL>,2011-11-03 00:19:26.831,0,info,0,<NULL>,<NULL>,<NULL>,1320279566.831473,CoX7zA3OJKGUOSCBY2,192.168.2.76,52027,72.21.211.173,80,1,GET,e.thumbs.redditmedia.com,/SVUtep3Rhg5FTRn4.jpg,http://www.reddit.com/,1.1,Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1,0,2562,200,OK,<NULL>,<NULL>,,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,F21Ybs3PTqS6O4Q2Zh,<NULL>,image/jpeg
4,<NULL>,2011-11-03 00:19:26.831,0,info,0,<NULL>,<NULL>,<NULL>,1320279566.831643,CT0JIh479jXIGt0Po1,192.168.2.76,52031,72.21.211.173,80,1,GET,f.thumbs.redditmedia.com,/uuy31444rLSyKdHS.jpg,http://www.reddit.com/,1.1,Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1,0,1595,200,OK,<NULL>,<NULL>,,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,Fdk0MZ1wQmKWAJ4WH4,<NULL>,image/jpeg
EOF

run_test env TZ=UTC ${lnav_test} -n \
    -c ";SELECT * FROM bro_http_log WHERE log_level = 'error'" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_bro_http.log.0

check_output "bro logs are not recognized?" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,bro_ts,bro_uid,bro_id_orig_h,bro_id_orig_p,bro_id_resp_h,bro_id_resp_p,bro_trans_depth,bro_method,bro_host,bro_uri,bro_referrer,bro_version,bro_user_agent,bro_request_body_len,bro_response_body_len,bro_status_code,bro_status_msg,bro_info_code,bro_info_msg,bro_tags,bro_username,bro_password,bro_proxied,bro_orig_fuids,bro_orig_filenames,bro_orig_mime_types,bro_resp_fuids,bro_resp_filenames,bro_resp_mime_types
118,<NULL>,2011-11-03 00:19:49.337,18,error,0,<NULL>,<NULL>,<NULL>,1320279589.337053,CBHHuR1xFnm5C5CQBc,192.168.2.76,52074,74.125.225.76,80,1,GET,i4.ytimg.com,/vi/gDbg_GeuiSY/hqdefault.jpg,<NULL>,1.1,Mozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1,0,893,404,Not Found,<NULL>,<NULL>,,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,<NULL>,F2GiAw3j1m22R2yIg2,<NULL>,image/jpeg
EOF

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line > 100000' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
EOF

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line > -100000' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
        log_time
2009-07-20 22:59:26.000
2009-07-20 22:59:29.000
2009-07-20 22:59:29.000
EOF

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line < -10000' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
EOF

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line > -10000' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
        log_time
2009-07-20 22:59:26.000
2009-07-20 22:59:29.000
2009-07-20 22:59:29.000
EOF

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line < 0' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "out-of-range query failed?" <<EOF
EOF

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line <= 0' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "range query failed?" <<EOF
        log_time
2009-07-20 22:59:26.000
EOF

run_test ${lnav_test} -n \
    -c ';select log_time from access_log where log_line >= 0' \
    -c ':switch-to-view db' \
    ${test_dir}/logfile_access_log.0

check_output "range query failed?" <<EOF
        log_time
2009-07-20 22:59:26.000
2009-07-20 22:59:29.000
2009-07-20 22:59:29.000
EOF


run_test ${lnav_test} -n \
    -c ';select sc_bytes from access_log' \
    -c ':spectrogram sc_bytes' \
    ${test_dir}/logfile_access_log.0

check_error_output "spectrogram worked without log_time?" <<EOF
command-option:2: error: no 'log_time' column found or not in ascending order, unable to create spectrogram
EOF

run_test ${lnav_test} -n \
    -c ';select log_time,sc_bytes from access_log' \
    -c ':spectrogram sc_byes' \
    ${test_dir}/logfile_access_log.0

check_error_output "spectrogram worked with bad column?" <<EOF
command-option:2: error: unknown column -- sc_byes
EOF

run_test ${lnav_test} -n \
    -c ';select log_time,c_ip from access_log' \
    -c ':spectrogram c_ip' \
    ${test_dir}/logfile_access_log.0

check_error_output "spectrogram worked with non-numeric column?" <<EOF
command-option:2: error: column is not numeric -- c_ip
EOF

run_test ${lnav_test} -n \
    -c ';select log_time,sc_bytes from access_log order by log_time desc' \
    -c ':spectrogram sc_bytes' \
    ${test_dir}/logfile_access_log.0

check_error_output "spectrogram worked with unordered log_time?" <<EOF
command-option:2: error: no 'log_time' column found or not in ascending order, unable to create spectrogram
EOF

cp ${srcdir}/logfile_syslog_with_mixed_times.0 logfile_syslog_with_mixed_times_test.0
touch -t 201511030923 logfile_syslog_with_mixed_times_test.0
run_test ${lnav_test} -n \
    -c ";select log_time,log_actual_time from syslog_log" \
    -c ':write-csv-to -' \
    logfile_syslog_with_mixed_times_test.0

check_output "log_actual_time column not working" <<EOF
log_time,log_actual_time
2015-09-13 00:58:45.000,2015-09-13 00:58:45.000
2015-09-13 00:59:30.000,2015-09-13 00:59:30.000
2015-09-13 01:23:54.000,2015-09-13 01:23:54.000
2015-09-13 03:12:04.000,2015-09-13 03:12:04.000
2015-09-13 03:12:04.000,2015-09-13 03:12:04.000
2015-09-13 03:12:04.000,2015-09-13 01:25:39.000
2015-09-13 03:12:04.000,2015-09-13 03:12:04.000
2015-09-13 03:12:58.000,2015-09-13 03:12:58.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
2015-09-13 03:46:03.000,2015-09-13 03:13:16.000
2015-09-13 03:46:03.000,2015-09-13 03:46:03.000
EOF


run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ';select log_line, log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part
0,<NULL>
1,middle
2,middle
EOF

run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ";update access_log set log_part = NULL where log_line = 1" \
    -c ';select log_line, log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part
0,<NULL>
1,<NULL>
2,<NULL>
EOF

run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ";update access_log set log_part = NULL where log_line = 2" \
    -c ';select log_line, log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part
0,<NULL>
1,middle
2,middle
EOF


run_test ${lnav_test} -n \
    -I "${top_srcdir}/test" \
    -c ";select * from web_status" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "access_log table is not working" <<EOF
group_concat(cs_uri_stem),sc_status
"/vmw/cgi/tramp,/vmw/vSphere/default/vmkernel.gz",200
/vmw/vSphere/default/vmkboot.gz,404
EOF


run_test ${lnav_test} -n \
    -c ";select * from access_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "access_log table is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status,cs_host
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,<NULL>,<NULL>,<NULL>,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200,<NULL>
1,<NULL>,2009-07-20 22:59:29.000,3000,error,0,<NULL>,<NULL>,<NULL>,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404,<NULL>
2,<NULL>,2009-07-20 22:59:29.000,0,info,0,<NULL>,<NULL>,<NULL>,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200,<NULL>
EOF


run_test ${lnav_test} -n \
    -c ";select * from access_log where log_level >= 'warning'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "loglevel collator is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status,cs_host
1,<NULL>,2009-07-20 22:59:29.000,3000,error,0,<NULL>,<NULL>,<NULL>,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404,<NULL>
EOF

run_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ";select log_line from access_log where log_level >= 'warning'" \
    -c ":switch-to-view log" \
    -c ":next-mark query" \
    ${test_dir}/logfile_access_log.0

check_output "query bookmark not working?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


# XXX The timestamp on the file is used to determine the year for syslog files.
touch -t 200711030923 ${test_dir}/logfile_syslog.0
run_test ${lnav_test} -n \
    -c ";select * from syslog_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "syslog_log table is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,log_hostname,log_msgid,log_pid,log_pri,log_procname,log_struct,syslog_version
0,<NULL>,2007-11-03 09:23:38.000,0,error,0,<NULL>,<NULL>,<NULL>,veridian,<NULL>,7998,<NULL>,automount,<NULL>,<NULL>
1,<NULL>,2007-11-03 09:23:38.000,0,info,0,<NULL>,<NULL>,<NULL>,veridian,<NULL>,16442,<NULL>,automount,<NULL>,<NULL>
2,<NULL>,2007-11-03 09:23:38.000,0,error,0,<NULL>,<NULL>,<NULL>,veridian,<NULL>,7999,<NULL>,automount,<NULL>,<NULL>
3,<NULL>,2007-11-03 09:47:02.000,1404000,info,0,<NULL>,<NULL>,<NULL>,veridian,<NULL>,<NULL>,<NULL>,sudo,<NULL>,<NULL>
EOF


run_test ${lnav_test} -n \
    -c ";select * from syslog_log where log_time >= NULL" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "log_time collation failed on null" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ";select * from syslog_log where log_time >= datetime('2007-11-03T09:47:02.000')" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "log_time collation is wrong" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,log_hostname,log_msgid,log_pid,log_pri,log_procname,log_struct,syslog_version
3,<NULL>,2007-11-03 09:47:02.000,1404000,info,0,<NULL>,<NULL>,<NULL>,veridian,<NULL>,<NULL>,<NULL>,sudo,<NULL>,<NULL>
EOF


run_test ${lnav_test} -n \
    -c ':filter-in sudo' \
    -c ";select * from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "logline table is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,log_hostname,log_msgid,log_pid,log_pri,log_procname,log_struct,syslog_version,log_msg_instance,col_0,TTY,PWD,USER,COMMAND
0,<NULL>,2007-11-03 09:47:02.000,0,info,0,<NULL>,<NULL>,[1],veridian,<NULL>,<NULL>,<NULL>,sudo,<NULL>,<NULL>,0,timstack,pts/6,/auto/wstimstack/rpms/lbuild/test,root,/usr/bin/tail /var/log/messages
EOF


run_test ${lnav_test} -n \
    -c ':goto 1' \
    -c ";select log_line, log_pid, col_0 from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.1

check_output "logline table is not working" <<EOF
log_line,log_pid,col_0
1,16442,/auto/opt
EOF

run_test ${lnav_test} -n \
    -c ";select sc_bytes from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "logline table is not working for defined columns" <<EOF
sc_bytes
134
46210
78929
EOF


run_test ${lnav_test} -n \
    -c ':goto 1' \
    -c ":summarize col_0" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.1

check_output "summarize is not working" <<EOF
c_col_0,count_col_0
/auto/opt,1
EOF


run_test ${lnav_test} -n \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ':write-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_mark is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


export SQL_ENV_VALUE="foo bar,baz"

run_test ${lnav_test} -n \
    -c ';select $SQL_ENV_VALUE as val' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "env vars are not working in SQL" <<EOF
val
"foo bar,baz"
EOF


run_test ${lnav_test} -n \
    -c ';SELECT name,value FROM environ WHERE name = "SQL_ENV_VALUE"' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "environ table is not working in SQL" <<EOF
name,value
SQL_ENV_VALUE,"foo bar,baz"
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name) VALUES (null)' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
command-option:1: error: A non-empty name and value must be provided when inserting an environment variable
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES (null, null)' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
command-option:1: error: A non-empty name and value must be provided when inserting an environment variable
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES ("", null)' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
command-option:1: error: A non-empty name and value must be provided when inserting an environment variable
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES ("foo=bar", "bar")' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
command-option:1: error: Environment variable names cannot contain an equals sign (=)
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES ("SQL_ENV_VALUE", "bar")' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
command-option:1: error: An environment variable with the name 'SQL_ENV_VALUE' already exists
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT OR IGNORE INTO environ (name, value) VALUES ("SQL_ENV_VALUE", "bar")' \
    -c ';SELECT * FROM environ WHERE name = "SQL_ENV_VALUE"' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "insert into environ table works" <<EOF
name,value
SQL_ENV_VALUE,"foo bar,baz"
EOF


run_test ${lnav_test} -n \
    -c ';REPLACE INTO environ (name, value) VALUES ("SQL_ENV_VALUE", "bar")' \
    -c ';SELECT * FROM environ WHERE name = "SQL_ENV_VALUE"' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "replace into environ table works" <<EOF
name,value
SQL_ENV_VALUE,bar
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES ("foo_env", "bar")' \
    -c ';SELECT $foo_env as val' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "insert into environ table does not work" <<EOF
val
bar
EOF


run_test ${lnav_test} -n \
    -c ';UPDATE environ SET name="NEW_ENV_VALUE" WHERE name="SQL_ENV_VALUE"' \
    -c ";SELECT * FROM environ WHERE name like '%ENV_VALUE'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "update environ table does not work" <<EOF
name,value
NEW_ENV_VALUE,"foo bar,baz"
EOF


run_test ${lnav_test} -n \
    -c ';DELETE FROM environ WHERE name="SQL_ENV_VALUE"' \
    -c ';SELECT * FROM environ WHERE name like "%ENV_VALUE"' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "delete from environ table does not work" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';DELETE FROM environ' \
    -c ';SELECT * FROM environ' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "delete environ table does not work" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';DELETE FROM lnav_views' \
    -c ';SELECT count(*) FROM lnav_views' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "delete from lnav_views table works?" <<EOF
count(*)
8
EOF


run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_views (name) VALUES ('foo')" \
    -c ';SELECT count(*) FROM lnav_views' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "insert into lnav_views table works?" <<EOF
count(*)
8
EOF


run_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top = 1 WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

check_output "updating lnav_views.top does not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top = inner_height - 1 WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

check_output "updating lnav_views.top using inner_height does not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top_time = 'bad-time' WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

check_error_output "updating lnav_views.top_time with a bad time works?" <<EOF
command-option:1: error: Invalid time: bad-time
EOF


run_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top_time = '2014-10-08T00:00:00' WHERE name = 'log'" \
    ${test_dir}/logfile_generic.0

check_output "updating lnav_views.top_time does not work?" <<EOF
2014-10-08 16:56:38,344:WARN:foo bar baz
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET search = 'warn' WHERE name = 'log'" \
    -c ";SELECT search FROM lnav_views WHERE name = 'log'" \
    ${test_dir}/logfile_generic.0

check_output "updating lnav_views.search does not work?" <<EOF
search
warn
EOF

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET search = 'warn' WHERE name = 'log'" \
    -c ":goto 0" \
    -c ":next-mark search" \
    ${test_dir}/logfile_generic.0

check_output "updating lnav_views.search does not work?" <<EOF
2014-10-08 16:56:38,344:WARN:foo bar baz
EOF


schema_dump() {
    ${lnav_test} -n -c ';.schema' ${test_dir}/logfile_access_log.0 | head -n19
}

run_test schema_dump

check_output "schema view is not working" <<EOF
ATTACH DATABASE '' AS 'main';
CREATE VIRTUAL TABLE environ USING environ_vtab_impl();
CREATE VIRTUAL TABLE lnav_views USING lnav_views_impl();
CREATE VIRTUAL TABLE lnav_view_filter_stats USING lnav_view_filter_stats_impl();
CREATE VIRTUAL TABLE lnav_view_files USING lnav_view_files_impl();
CREATE VIRTUAL TABLE lnav_view_stack USING lnav_view_stack_impl();
CREATE VIRTUAL TABLE lnav_view_filters USING lnav_view_filters_impl();
CREATE VIRTUAL TABLE lnav_file USING lnav_file_impl();
CREATE VIEW lnav_view_filters_and_stats AS
  SELECT * FROM lnav_view_filters LEFT NATURAL JOIN lnav_view_filter_stats;
CREATE VIRTUAL TABLE regexp_capture USING regexp_capture_impl();
CREATE VIRTUAL TABLE xpath USING xpath_impl();
CREATE VIRTUAL TABLE fstat USING fstat_impl();
CREATE TABLE http_status_codes (
    status integer PRIMARY KEY,
    message text,

    FOREIGN KEY(status) REFERENCES access_log(sc_status)
);
EOF


run_test ${lnav_test} -n \
    -c ";select * from nonexistent_table" \
    ${test_dir}/logfile_access_log.0

check_error_output "errors are not reported" <<EOF
command-option:1: error: no such table: nonexistent_table
EOF

check_output "errors are not reported" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ";delete from access_log" \
    ${test_dir}/logfile_access_log.0

check_error_output "errors are not reported" <<EOF
command-option:1: error: attempt to write a readonly database
EOF

check_output "errors are not reported" <<EOF
EOF


touch -t 201504070732 ${test_dir}/logfile_pretty.0
run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":goto 21" \
    -c ":partition-name end" \
    -c ";select log_line,log_part,log_time from syslog_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_pretty.0

check_output "partition-name does not work" <<EOF
log_line,log_part,log_time
0,<NULL>,2015-04-07 00:49:42.000
1,middle,2015-04-07 05:49:53.000
18,middle,2015-04-07 07:31:56.000
20,middle,2015-04-07 07:31:56.000
21,end,2015-04-07 07:31:56.000
22,end,2015-04-07 07:32:56.000
EOF

run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":clear-partition" \
    -c ";select log_line, log_part from access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "clear-partition does not work" <<EOF
log_line,log_part
0,<NULL>
1,<NULL>
2,<NULL>
EOF

run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":goto 2" \
    -c ":clear-partition" \
    -c ";select log_line, log_part from access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "clear-partition does not work when in the middle of a part" <<EOF
log_line,log_part
0,<NULL>
1,<NULL>
2,<NULL>
EOF


run_test ${lnav_test} -n \
    -c ";SELECT * FROM openam_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_openam.0

check_output "write-json-to isn't working?" <<EOF
[
    {
        "log_line": 0,
        "log_part": null,
        "log_time": "2014-06-15 01:04:52.000",
        "log_idle_msecs": 0,
        "log_level": "info",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": null,
        "contextid": "82e87195d704585501",
        "data": "http://localhost:8086|/|<samlp:Response xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\" ID=\"s2daac0735bf476f4560aab81104b623bedfb0cbc0\" InResponseTo=\"84cbf2be33f6410bbe55877545a93f02\" Version=\"2.0\" IssueInstant=\"2014-06-15T01:04:52Z\" Destination=\"http://localhost:8086/api/1/rest/admin/org/530e42ccd6f45fd16d0d0717/saml/consume\"><saml:Issuer xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\">http://openam.vagrant.dev/openam</saml:Issuer><samlp:Status xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\">\\\\n<samlp:StatusCode  xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\"\\\\nValue=\"urn:oasis:names:tc:SAML:2.0:status:Success\">\\\\n</samlp:StatusCode>\\\\n</samlp:Status><saml:Assertion xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\" ID=\"s2a0bee0da937e236167e99b209802056033816ac2\" IssueInstant=\"2014-06-15T01:04:52Z\" Version=\"2.0\">\\\\n<saml:Issuer>http://openam.vagrant.dev/openam</saml:Issuer><ds:Signature xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\">\\\\n<ds:SignedInfo>\\\\n<ds:CanonicalizationMethod Algorithm=\"http://www.w3.org/2001/10/xml-exc-c14n#\"/>\\\\n<ds:SignatureMethod Algorithm=\"http://www.w3.org/2000/09/xmldsig#rsa-sha1\"/>\\\\n<ds:Reference URI=\"#s2a0bee0da937e236167e99b209802056033816ac2\">\\\\n<ds:Transforms>\\\\n<ds:Transform Algorithm=\"http://www.w3.org/2000/09/xmldsig#enveloped-signature\"/>\\\\n<ds:Transform Algorithm=\"http://www.w3.org/2001/10/xml-exc-c14n#\"/>\\\\n</ds:Transforms>\\\\n<ds:DigestMethod Algorithm=\"http://www.w3.org/2000/09/xmldsig#sha1\"/>\\\\n<ds:DigestValue>4uSmVzjovUdQd3px/RcnoxQBsqE=</ds:DigestValue>\\\\n</ds:Reference>\\\\n</ds:SignedInfo>\\\\n<ds:SignatureValue>\\\\nhm/grge36uA6j1OWif2bTcvVTwESjmuJa27NxepW0AiV5YlcsHDl7RAIk6k/CjsSero3bxGbm56m\\\\nYncOEi9F1Tu7dS0bfx+vhm/kKTPgwZctf4GWn4qQwP+KeoZywbNj9ShsYJ+zPKzXwN4xBSuPjMxP\\\\nNf5szzjEWpOndQO/uDs=\\\\n</ds:SignatureValue>\\\\n<ds:KeyInfo>\\\\n<ds:X509Data>\\\\n<ds:X509Certificate>\\\\nMIICQDCCAakCBEeNB0swDQYJKoZIhvcNAQEEBQAwZzELMAkGA1UEBhMCVVMxEzARBgNVBAgTCkNh\\\\nbGlmb3JuaWExFDASBgNVBAcTC1NhbnRhIENsYXJhMQwwCgYDVQQKEwNTdW4xEDAOBgNVBAsTB09w\\\\nZW5TU08xDTALBgNVBAMTBHRlc3QwHhcNMDgwMTE1MTkxOTM5WhcNMTgwMTEyMTkxOTM5WjBnMQsw\\\\nCQYDVQQGEwJVUzETMBEGA1UECBMKQ2FsaWZvcm5pYTEUMBIGA1UEBxMLU2FudGEgQ2xhcmExDDAK\\\\nBgNVBAoTA1N1bjEQMA4GA1UECxMHT3BlblNTTzENMAsGA1UEAxMEdGVzdDCBnzANBgkqhkiG9w0B\\\\nAQEFAAOBjQAwgYkCgYEArSQc/U75GB2AtKhbGS5piiLkmJzqEsp64rDxbMJ+xDrye0EN/q1U5Of+\\\\nRkDsaN/igkAvV1cuXEgTL6RlafFPcUX7QxDhZBhsYF9pbwtMzi4A4su9hnxIhURebGEmxKW9qJNY\\\\nJs0Vo5+IgjxuEWnjnnVgHTs1+mq5QYTA7E6ZyL8CAwEAATANBgkqhkiG9w0BAQQFAAOBgQB3Pw/U\\\\nQzPKTPTYi9upbFXlrAKMwtFf2OW4yvGWWvlcwcNSZJmTJ8ARvVYOMEVNbsT4OFcfu2/PeYoAdiDA\\\\ncGy/F2Zuj8XJJpuQRSE6PtQqBuDEHjjmOQJ0rV/r8mO1ZCtHRhpZ5zYRjhRC9eCbjx9VrFax0JDC\\\\n/FfwWigmrW0Y0Q==\\\\n</ds:X509Certificate>\\\\n</ds:X509Data>\\\\n</ds:KeyInfo>\\\\n</ds:Signature><saml:Subject>\\\\n<saml:NameID Format=\"urn:oasis:names:tc:SAML:1.1:nameid-format:emailAddress\" NameQualifier=\"http://openam.vagrant.dev/openam\">user@example.com</saml:NameID><saml:SubjectConfirmation Method=\"urn:oasis:names:tc:SAML:2.0:cm:bearer\">\\\\n<saml:SubjectConfirmationData InResponseTo=\"84cbf2be33f6410bbe55877545a93f02\" NotOnOrAfter=\"2014-06-15T01:14:52Z\" Recipient=\"http://localhost:8086/api/1/rest/admin/org/530e42ccd6f45fd16d0d0717/saml/consume\"/></saml:SubjectConfirmation>\\\\n</saml:Subject><saml:Conditions NotBefore=\"2014-06-15T00:54:52Z\" NotOnOrAfter=\"2014-06-15T01:14:52Z\">\\\\n<saml:AudienceRestriction>\\\\n<saml:Audience>http://localhost:8086</saml:Audience>\\\\n</saml:AudienceRestriction>\\\\n</saml:Conditions>\\\\n<saml:AuthnStatement AuthnInstant=\"2014-06-15T01:00:25Z\" SessionIndex=\"s2f9b4d4b453d12b40ef3905cc959cdb40579c2301\"><saml:AuthnContext><saml:AuthnContextClassRef>urn:oasis:names:tc:SAML:2.0:ac:classes:PasswordProtectedTransport</saml:AuthnContextClassRef></saml:AuthnContext></saml:AuthnStatement></saml:Assertion></samlp:Response>",
        "domain": "dc=openam",
        "hostname": "192.168.33.1\t",
        "ipaddr": "Not Available",
        "loggedby": "cn=dsameuser,ou=DSAME Users,dc=openam",
        "loginid": "id=openamuser,ou=user,dc=openam",
        "messageid": "SAML2-37",
        "modulename": "SAML2.access",
        "nameid": "user@example.com"
    },
    {
        "log_line": 1,
        "log_part": null,
        "log_time": "2014-06-15 01:04:52.000",
        "log_idle_msecs": 0,
        "log_level": "trace",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": null,
        "contextid": "ec5708a7f199678a01",
        "data": "vagrant|/",
        "domain": "dc=openam",
        "hostname": "127.0.1.1\t",
        "ipaddr": "Not Available",
        "loggedby": "cn=dsameuser,ou=DSAME Users,dc=openam",
        "loginid": "cn=dsameuser,ou=DSAME Users,dc=openam",
        "messageid": "COT-22",
        "modulename": "COT.access",
        "nameid": "Not Available"
    }
]
EOF

run_test ${lnav_test} -d "/tmp/lnav.err" -n \
    -c ";select log_line, log_msg_instance, col_0 from logline" \
    ${test_dir}/logfile_for_join.0

check_output "log msg instance is not working" <<EOF
log_line log_msg_instance   col_0
       0                0 eth0.IPv4
       7                1 eth0.IPv4
EOF

run_test ${lnav_test} -d "/tmp/lnav.err" -n \
    -c ";select log_msg_instance, col_0 from logline where log_line > 4" \
    ${test_dir}/logfile_for_join.0

check_output "log msg instance is not working" <<EOF
log_msg_instance   col_0
               1 eth0.IPv4
EOF

run_test ${lnav_test} -d "/tmp/lnav.err" -n \
    -c ":goto 1" \
    -c ":create-logline-table join_group" \
    -c ":goto 2" \
    -c ";select logline.log_line as llline, join_group.log_line as jgline from logline, join_group where logline.col_0 = join_group.col_2" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_for_join.0

check_output "create-logline-table is not working" <<EOF
llline,jgline
2,1
2,8
9,1
9,8
EOF


cat ${test_dir}/logfile_syslog.0 | run_test ${lnav_test} -n \
    -c ";select log_body from syslog_log where log_procname = 'automount'"

check_output "querying against stdin is not working?" <<EOF
                log_body
 lookup(file): lookup for foobar failed
 attempting to mount entry /auto/opt
 lookup(file): lookup for opt failed
EOF


cat ${test_dir}/logfile_syslog.0 | run_test ${lnav_test} -n \
    -c ";select log_body from syslog_log where log_procname = 'sudo'"

check_output "single result is not working?" <<EOF
                                                      log_body
 timstack : TTY=pts/6 ; PWD=/auto/wstimstack/rpms/lbuild/test ; USER=root ; COMMAND=/usr/bin/tail /var/log/messages
EOF

# Create a dummy database for the next couple of tests to consume.
touch empty
run_test ${lnav_test} -n \
    -c ";ATTACH DATABASE 'simple-db.db' as 'db'" \
    -c ";CREATE TABLE IF NOT EXISTS db.person ( id integer PRIMARY KEY, first_name text, last_name, age integer )" \
    -c ";INSERT INTO db.person(id, first_name, last_name, age) VALUES (0, 'Phil', 'Myman', 30)" \
    -c ";INSERT INTO db.person(id, first_name, last_name, age) VALUES (1, 'Lem', 'Hewitt', 35)" \
    -c ";DETACH DATABASE 'db'" \
    empty

check_output "Could not create db?" <<EOF
EOF

# Test to see if lnav can recognize a sqlite3 db file passed in as an argument.
run_test ${lnav_test} -n -c ";select * from person order by age asc" \
    simple-db.db

check_output "lnav not able to recognize sqlite3 db file?" <<EOF
id first_name last_name age
 0 Phil       Myman      30
 1 Lem        Hewitt     35
EOF

# Test to see if lnav can recognize a sqlite3 db file passed in as an argument.
# XXX: Need to pass in a file, otherwise lnav keeps trying to open syslog
# and we might not have sufficient privileges on the system the tests are being
# run on.
run_test ${lnav_test} -n \
    -c ";attach database 'simple-db.db' as 'db'" \
    -c ';select * from person order by age asc' \
    empty

check_output "lnav not able to attach sqlite3 db file?" <<EOF
id first_name last_name age
 0 Phil       Myman      30
 1 Lem        Hewitt     35
EOF

# Test to see if we can attach a database in LNAVSECURE mode.
export LNAVSECURE=1

run_test ${lnav_test} -n \
    -c ";attach database 'simple-db.db' as 'db'" \
    empty

check_error_output "LNAVSECURE mode bypassed" <<EOF
command-option:1: error: not authorized
EOF

run_test ${lnav_test} -n \
    -c ";attach database ':memdb:' as 'db'" \
    empty

check_error_output "LNAVSECURE mode bypassed (':' adorned)" <<EOF
command-option:1: error: not authorized
EOF

run_test ${lnav_test} -n \
    -c ";attach database '/tmp/memdb' as 'db'" \
    empty

check_error_output "LNAVSECURE mode bypassed (filepath)" <<EOF
command-option:1: error: not authorized
EOF

run_test ${lnav_test} -n \
    -c ";attach database 'file:memdb?cache=shared' as 'db'" \
    empty

check_error_output "LNAVSECURE mode bypassed (URI)" <<EOF
command-option:1: error: not authorized
EOF

unset LNAVSECURE


touch -t 201503240923 ${test_dir}/logfile_syslog_with_access_log.0
run_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ";select * from access_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog_with_access_log.0

check_output "access_log not found within syslog file" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status,cs_host
1,<NULL>,2015-03-24 14:02:50.000,6927348000,info,0,<NULL>,<NULL>,<NULL>,127.0.0.1,GET,<NULL>,<NULL>,/includes/js/combined-javascript.js,<NULL>,-,HTTP/1.1,65508,200,<NULL>
2,<NULL>,2015-03-24 14:02:50.000,0,error,0,<NULL>,<NULL>,<NULL>,127.0.0.1,GET,<NULL>,<NULL>,/bad.foo,<NULL>,-,HTTP/1.1,65508,404,<NULL>
EOF


run_test ${lnav_test} -n \
    -c ";select log_text from generic_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_multiline.0

check_output "multiline data is not right?" <<EOF
[
    {
        "log_text": "2009-07-20 22:59:27,672:DEBUG:Hello, World!\n  How are you today?"
    },
    {
        "log_text": "2009-07-20 22:59:30,221:ERROR:Goodbye, World!"
    }
]
EOF

run_test ${lnav_test} -n \
    -c ";select log_text from generic_log where log_line = 1" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_multiline.0

check_output "able to select a continued line?" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 (\w+), world!" \
    -c ";select log_msg_instance, col_0 from search_test1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "create-search-table is not working?" <<EOF
log_msg_instance,col_0
0,Hello
1,Goodbye
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 (\w+), World!" \
    -c ";select log_msg_instance, col_0 from search_test1 where log_line > 0" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "create-search-table is not working with where clause?" <<EOF
log_msg_instance,col_0
1,Goodbye
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 (?<word>\w+), World!" \
    -c ";select word, typeof(word) from search_test1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "create-search-table is not working?" <<EOF
word,typeof(word)
Hello,text
Goodbye,text
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 eth(?<ethnum>\d+)" \
    -c ";select typeof(ethnum) from search_test1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_syslog.2

check_output "regex type guessing is not working?" <<EOF
typeof(ethnum)
integer
integer
integer
EOF

run_test ${lnav_test} -n \
    -c ":delete-search-table search_test1" \
    ${test_dir}/logfile_multiline.0

check_error_output "able to delete unknown table?" <<EOF
command-option:1: error: unknown search table -- search_test1
EOF

run_test ${lnav_test} -n \
    -c ":create-logline-table search_test1" \
    -c ":delete-search-table search_test1" \
    ${test_dir}/logfile_multiline.0

check_error_output "able to delete logline table?" <<EOF
command-option:2: error: unknown search table -- search_test1
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 bad(" \
    ${test_dir}/logfile_multiline.0

check_error_output "able to create table with a bad regex?" <<EOF
command-option:1: error: missing )
EOF

NULL_GRAPH_SELECT_1=$(cat <<EOF
;SELECT value FROM (
              SELECT 10 as value
    UNION ALL SELECT null as value)
EOF
)

run_test ${lnav_test} -n \
    -c "$NULL_GRAPH_SELECT_1" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_multiline.0

check_output "number column with null does not work?" <<EOF
value
10
<NULL>
EOF

run_test ${lnav_test} -n \
    -c ";SELECT regexp_capture.content FROM access_log, regexp_capture(access_log.cs_version, 'HTTP/(\d+\.\d+)') WHERE regexp_capture.capture_index = 1" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "joining log table with regexp_capture is not working?" <<EOF
content
1.0
1.0
1.0
EOF
