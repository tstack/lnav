#! /bin/bash

export TZ=UTC
export YES_COLOR=1

# journald json log format is not working"
run_cap_test env TZ=UTC ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_journald.json

# json log format is not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':filter-in up service' \
    ${test_dir}/logfile_json.json

# json log format is not working"
run_cap_test ${lnav_test} -n -I ${test_dir} \
    -c ':switch-to-view pretty' \
    -c ':switch-to-view log' \
    -c ':switch-to-view pretty' \
    ${test_dir}/logfile_json.json

# multi-line-format json log format is not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/log.clog

# log levels not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from test_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json.json

# log levels not working" < ${test_dir}/logfile_jso
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select log_raw_text from test_log' \
    -c ':write-raw-to -' \
    ${test_dir}/logfile_json.json

# write-raw-to with json is not working" <
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':goto 0' \
    -c ':mark' \
    -c ':goto 1' \
    -c ':mark' \
    -c ':goto 2' \
    -c ':mark' \
    -c ':write-raw-to -' \
    ${test_dir}/log.clog

# json output not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from test_log' \
    -c ':write-json-to -' \
    ${test_dir}/logfile_json.json

# timestamp-format not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json2.json

# log levels not working"
run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -I ${test_dir} \
    -c ';select * from json_log2' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json2.json

# pipe-line-to is not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 4" \
    -c ":pipe-line-to sed -e 's/2013//g'" \
    -c ":switch-to-view text" \
    ${test_dir}/logfile_json.json

# json log format is not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_nested_json.json

# log levels not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from ntest_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_nested_json.json

# pipe-line-to is not working"
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 4" \
    -c ":pipe-line-to sed -e 's/2013//g'" \
    -c ":switch-to-view text" \
    ${test_dir}/logfile_nested_json.json

# json log3 format is not working"
run_cap_test env TZ=UTC ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json3.json

# json log3 format is not working"
run_cap_test env TZ=UTC ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from json_log3' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json3.json

run_cap_test env TZ=America/New_York ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json3.json

# json log3 format is not working"
run_cap_test env TZ=America/New_York ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from json_log3' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json3.json

# json log format is not working"
run_cap_test ${lnav_test} -n \
    -d /tmp/lnav.err \
    -I ${test_dir} \
    ${test_dir}/logfile_invalid_json.json

# json log format is not working"
run_cap_test ${lnav_test} -n \
    -d /tmp/lnav.err \
    -I ${test_dir} \
    ${test_dir}/logfile_invalid_json2.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_mixed_json2.json

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json_subsec.json

run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_bunyan.0

run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_cloudflare.json

run_cap_test ${lnav_test} -n \
    -c ':show-fields RayID' \
    ${test_dir}/logfile_cloudflare.json

run_cap_test ${lnav_test} -n \
    ${test_dir}/logfile_nextcloud.0

run_cap_test ${lnav_test} -n \
    ${test_dir}/gharchive_log.jsonl
