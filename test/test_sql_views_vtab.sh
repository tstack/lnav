#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

run_test ${lnav_test} -n \
    -c ";SELECT view_name,basename(filepath),visible FROM lnav_view_files" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.*

check_output "lnav_view_files does not work?" <<EOF
view_name,basename(filepath),visible
log,logfile_access_log.0,1
log,logfile_access_log.1,1
EOF

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_view_files SET visible=0 WHERE endswith(filepath, 'log.0')" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_files VALUES ('log', '/abc/def', 1)" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n \
    -c ";DELETE FROM lnav_view_files" \
    ${test_dir}/logfile_access_log.*

run_test ${lnav_test} -n \
    -c ";DELETE FROM lnav_view_stack" \
    ${test_dir}/logfile_access_log.0

check_output "deleting the view stack does not work?" <<EOF
EOF

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_view_stack SET name = 'foo'" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_stack VALUES ('help')" \
    -c ";DELETE FROM lnav_view_stack WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -nN \
    -c ";INSERT INTO lnav_view_filters VALUES ('log', 0, 1, 'out', 'bad', '')"

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('log', 0, 1, 'out', 'regex', '')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('log', 0, 1, 'out', 'regex', 'abc(')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('bad', 0, 1, 'out', 'regex', 'abc')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES (NULL, 0, 1, 'out', 'regex', 'abc')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters VALUES ('log', 0 , 1, 'bad', 'regex', 'abc')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern) VALUES ('log', 'vmk')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern, type) VALUES ('log', 'vmk', 'in')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern, type) VALUES ('log', 'vmk', 'in')" \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern, type) VALUES ('log', 'vmk', 'in')" \
    -c ';SELECT * FROM lnav_view_filters' \
    -c ':write-screen-to -' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern, type) VALUES ('log', 'vmk', 'in')" \
    -c ";INSERT INTO lnav_view_filters (view_name, pattern, type) VALUES ('log', 'vmk1', 'in')" \
    -c ";UPDATE lnav_view_filters SET pattern = 'vmk'" \
    -c ';SELECT * FROM lnav_view_filters' \
    -c ':write-screen-to -' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, language, pattern) VALUES ('log', 'sql', '1')" \
    -c ";INSERT INTO lnav_view_filters (view_name, language, pattern) VALUES ('log', 'sql', '1')" \
    -c ';SELECT * FROM lnav_view_filters' \
    -c ':write-screen-to -' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    -c ";DELETE FROM lnav_view_filters" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    -c ";UPDATE lnav_view_filters SET pattern = 'vmkboot'" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    -c ";UPDATE lnav_view_filters SET enabled = 0" \
    ${test_dir}/logfile_access_log.0

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
    -c ";INSERT INTO lnav_view_filters (view_name, language, pattern) VALUES ('log', 'sql', ':sc_bytes = 134')" \
    ${test_dir}/logfile_access_log.0

check_output "inserted filter-out did not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
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

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top = 1 WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

run_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top = inner_height - 1 WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

check_output "updating lnav_views.top using inner_height does not work?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF


run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top_time = 'bad-time' WHERE name = 'log'" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top_time = '2014-10-08T00:00:00' WHERE name = 'log'" \
    ${test_dir}/logfile_generic.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET search = 'warn' WHERE name = 'log'" \
    -c ";SELECT search FROM lnav_views WHERE name = 'log'" \
    ${test_dir}/logfile_generic.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET search = 'warn' WHERE name = 'log'" \
    -c ":goto 0" \
    -c ":next-mark search" \
    ${test_dir}/logfile_generic.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top_meta = json_object('anchor', '#build') WHERE name = 'text'" \
    ${top_srcdir}/README.md

run_cap_test ${lnav_test} -n \
    -c ":goto 5" \
    -c ";SELECT top_meta FROM lnav_top_view" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_xml_msg.0

run_cap_test ${lnav_test} -n -I ${test_dir} \
    -c ";UPDATE lnav_views SET options = json_object('row-details', 'show') WHERE name = 'log'" \
    -c ":goto 2" \
    ${test_dir}/logfile_xml_msg.0

run_cap_test ${lnav_test} -n -I ${test_dir} \
    -c ";UPDATE lnav_views SET options = json_object('row-details', 'show') WHERE name = 'log'" \
    -c ":goto 9" \
    ${test_dir}/logfile_bunyan.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET options = json_object('row-time-offset', 'show') WHERE name = 'log'" \
    ${test_dir}/logfile_w3c_big.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE lnav_views SET top_meta = json_object('file', 'bad') WHERE name = 'text'" \
    ${test_dir}/textfile_ansi.0

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM lnav_views" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, language, pattern) VALUES ('text', 'sql', ':sc_bytes = 134')" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";INSERT INTO lnav_view_filters (view_name, language, pattern) VALUES ('log', 'sql', ':sc_bytes # 134')" \
    ${test_dir}/logfile_access_log.0
