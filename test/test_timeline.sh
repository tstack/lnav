#! /bin/bash

export YES_COLOR=1

export TZ=UTC

touch -t 200711030923 ${srcdir}/logfile_glog.0
run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ";UPDATE all_logs set log_opid = 'test1' where log_line in (1, 3, 6)" \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_glog.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_generic.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':filter-in CdysLK1XpcrXOpVDuh' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':filter-out CdysLK1XpcrXOpVDuh' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-file *' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':close *' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-lines-before 2011-11-03 00:19:37' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':hide-lines-after 2011-11-03 00:20:30' \
    ${test_dir}/logfile_bro_http.log.0
