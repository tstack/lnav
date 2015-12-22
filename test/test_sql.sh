#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ';select * from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200
1,middle,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
2,middle,2009-07-20 22:59:29.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200
EOF

run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ";update access_log set log_part = NULL where log_line = 1" \
    -c ';select * from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200
1,<NULL>,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
2,<NULL>,2009-07-20 22:59:29.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200
EOF

run_test ${lnav_test} -n \
    -c ";update access_log set log_part = 'middle' where log_line = 1" \
    -c ";update access_log set log_part = NULL where log_line = 2" \
    -c ';select * from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "setting log_part is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200
1,middle,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
2,middle,2009-07-20 22:59:29.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200
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
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200
1,<NULL>,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
2,<NULL>,2009-07-20 22:59:29.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200
EOF


run_test ${lnav_test} -n \
    -c ";select * from access_log where log_level >= 'warning'" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "loglevel collator is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
1,<NULL>,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
EOF


# XXX The timestamp on the file is used to determine the year for syslog files.
touch -t 201311030923 ${test_dir}/logfile_syslog.0
run_test ${lnav_test} -n \
    -c ";select * from syslog_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "syslog_log table is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_hostname,log_pid,log_procname
0,<NULL>,2013-11-03 09:23:38.000,0,error,0,veridian,7998,automount
1,<NULL>,2013-11-03 09:23:38.000,0,info,0,veridian,16442,automount
2,<NULL>,2013-11-03 09:23:38.000,0,error,0,veridian,7999,automount
3,<NULL>,2013-11-03 09:47:02.000,1404000,info,0,veridian,<NULL>,sudo
EOF


run_test ${lnav_test} -n \
    -c ";select * from syslog_log where log_time >= datetime('2013-11-03T09:47:02.000')" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "log_time collation is wrong" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_hostname,log_pid,log_procname
3,<NULL>,2013-11-03 09:47:02.000,1404000,info,0,veridian,<NULL>,sudo
EOF


run_test ${lnav_test} -n \
    -c ':filter-in sudo' \
    -c ";select * from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.0

check_output "logline table is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_hostname,log_pid,log_procname,log_msg_instance,col_0,TTY,PWD,USER,COMMAND
0,<NULL>,2013-11-03 09:47:02.000,0,info,0,veridian,<NULL>,sudo,0,timstack,pts/6,/auto/wstimstack/rpms/lbuild/test,root,/usr/bin/tail /var/log/messages
EOF


run_test ${lnav_test} -n \
    -c ':goto 1' \
    -c ";select * from logline" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog.1

check_output "logline table is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_hostname,log_pid,log_procname,log_msg_instance,col_0
1,<NULL>,2006-12-03 09:23:38.000,0,info,0,veridian,16442,automount,0,/auto/opt
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
error: A non-empty name and value must be provided when inserting an environment variable
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES (null, null)' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
error: A non-empty name and value must be provided when inserting an environment variable
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES ("", null)' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
error: A non-empty name and value must be provided when inserting an environment variable
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES ("foo=bar", "bar")' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
error: Environment variable names cannot contain an equals sign (=)
EOF

check_output "insert into environ table works" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ';INSERT INTO environ (name, value) VALUES ("SQL_ENV_VALUE", "bar")' \
    ${test_dir}/logfile_access_log.0

check_error_output "insert into environ table works" <<EOF
error: An environment variable with the name 'SQL_ENV_VALUE' already exists
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
    -c ';SELECT * FROM environ WHERE name like "%ENV_VALUE"' \
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
9
EOF


run_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_views (name) VALUES ('foo')" \
    -c ';SELECT count(*) FROM lnav_views' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

check_output "insert into lnav_views table works?" <<EOF
count(*)
9
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


schema_dump() {
    ${lnav_test} -n -c ';.schema' ${test_dir}/logfile_access_log.0 | head -n9
}

run_test schema_dump

check_output "schema view is not working" <<EOF
ATTACH DATABASE '' AS 'main';
CREATE VIRTUAL TABLE environ USING environ_vtab_impl();
CREATE VIRTUAL TABLE lnav_views USING views_vtab_impl();
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
error: no such table: nonexistent_table
EOF

check_output "errors are not reported" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ";delete from access_log" \
    ${test_dir}/logfile_access_log.0

check_error_output "errors are not reported" <<EOF
error: attempt to write a readonly database
EOF

check_output "errors are not reported" <<EOF
EOF


run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ";select * from access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "partition-name does not work" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200
1,middle,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
2,middle,2009-07-20 22:59:29.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200
EOF


run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":clear-partition" \
    -c ";select * from access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "clear-partition does not work" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200
1,<NULL>,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
2,<NULL>,2009-07-20 22:59:29.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200
EOF

run_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":goto 2" \
    -c ":clear-partition" \
    -c ";select * from access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

check_output "clear-partition does not work when in the middle of a part" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
0,<NULL>,2009-07-20 22:59:26.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/cgi/tramp,gPXE/0.9.7,-,HTTP/1.0,134,200
1,<NULL>,2009-07-20 22:59:29.000,3000,error,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkboot.gz,gPXE/0.9.7,-,HTTP/1.0,46210,404
2,<NULL>,2009-07-20 22:59:29.000,0,info,0,192.168.202.254,GET,-,<NULL>,/vmw/vSphere/default/vmkernel.gz,gPXE/0.9.7,-,HTTP/1.0,78929,200
EOF


run_test ${lnav_test} -n \
    -c ";SELECT * FROM openam_log" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_openam.0

check_output "" <<EOF
[
    {
        "log_line": 0,
        "log_part": null,
        "log_time": "2014-06-15 01:04:52.000",
        "log_idle_msecs": 0,
        "log_level": "info",
        "log_mark": 0,
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
    -c ";select * from syslog_log where log_procname = 'automount'"

check_output "querying against stdin is not working?" <<EOF
log_line log_part         log_time        log_idle_msecs log_level log_mark log_hostname log_pid log_procname
       0 <NULL>      2015-11-03 09:23:38.000              0 error            0 veridian     7998    automount
       1 <NULL>      2015-11-03 09:23:38.000              0 info             0 veridian     16442   automount
       2 <NULL>      2015-11-03 09:23:38.000              0 error            0 veridian     7999    automount
EOF


cat ${test_dir}/logfile_syslog.0 | run_test ${lnav_test} -n \
    -c ";select * from syslog_log where log_procname = 'sudo'"

check_output "single result is not working?" <<EOF
log_line log_part         log_time        log_idle_msecs log_level log_mark log_hostname log_pid log_procname
       3 <NULL>      2015-11-03 09:47:02.000        1404000 info             0 veridian      <NULL> sudo
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


run_test ${lnav_test} -n \
    -c ";select * from access_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_syslog_with_access_log.0

check_output "access_log not found within syslog file" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
1,<NULL>,2015-03-24 14:02:50.000,6927348000,info,0,127.0.0.1,GET,<NULL>,<NULL>,/includes/js/combined-javascript.js,<NULL>,-,HTTP/1.1,65508,200
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
error: unknown search table -- search_test1
EOF

run_test ${lnav_test} -n \
    -c ":create-logline-table search_test1" \
    -c ":delete-search-table search_test1" \
    ${test_dir}/logfile_multiline.0

check_error_output "able to delete logline table?" <<EOF
error: unknown search table -- search_test1
EOF

run_test ${lnav_test} -n \
    -c ":create-search-table search_test1 bad(" \
    ${test_dir}/logfile_multiline.0

check_error_output "able to create table with a bad regex?" <<EOF
error: unable to compile regex -- bad(
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
