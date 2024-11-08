#! /bin/bash

export TZ=UTC
export YES_COLOR=1
export DUMP_CRASH=1

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":convert-time-to bad-zone" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":convert-time-to America/Los_Angeles" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -nN \
    -c ";SELECT ':echo Hello' || char(10) || ':echo World' AS cmds" \
    -c ':eval ${cmds}'

run_cap_test ${lnav_test} -nN \
    -c ":cd /bad-dir"

run_cap_test ${lnav_test} -nN \
    -c ":cd ${test_dir}/logfile_access_log.0"

run_cap_test ${lnav_test} -nN \
    -c ":cd ${test_dir}" \
    -c ":open logfile_access_log.0"

run_cap_test ${lnav_test} -nN \
    -e "echo Hello, World!"

run_cap_test ${lnav_test} -nN \
    -e "echo Hello, World! > /dev/stderr"

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view help" \
    ${test_dir}/logfile_access_log.0

run_cap_test env TZ=UTC ${lnav_test} -n \
    -c ":goto 2011-11-03 00:19:39" \
    -c ";SELECT log_top_line()" \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":mark" \
    -c ":hide-unmarked-lines" \
    -c ":goto 0" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":unix-time" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n \
    -c ":unix-time abc" \
    "${test_dir}/logfile_access_log.*"

run_cap_test env TZ=UTC ${lnav_test} -n \
    -c ":unix-time 1612072409" \
    "${test_dir}/logfile_access_log.*"

#run_cap_test env TZ=UTC ${lnav_test} -n \
#    -c ":unix-time 16120724091612072409" \
#    "${test_dir}/logfile_access_log.*"

run_cap_test env TZ=UTC ${lnav_test} -n \
    -c ":current-time" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":write-to" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ";SELECT 1 AS c1, 'Hello ' || char(10) || 'World!' AS c2" \
    -c ":write-csv-to -" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ";SELECT 1 AS c1, 'Hello, World!' AS c2" \
    -c ":write-table-to -" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ";SELECT 1 AS c1, 'Hello, World!' AS c2" \
    -c ":write-raw-to -" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":write-view-to -" \
    "${test_dir}/logfile_access_log.0"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":write-view-to --anonymize -" \
    "${test_dir}/logfile_access_log.0"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":write-view-to --anonymize -" \
    "${test_dir}/logfile_pretty.0"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":filter-expr timeslice(:log_time_msecs, 'bad') is not null" \
    "${test_dir}/logfile_multiline.0"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":filter-expr :log_text LIKE '%How are%'" \
    "${test_dir}/logfile_multiline.0"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":filter-expr not json_contains(:log_tags, '#bad')" \
    -c ":goto 0" \
    -c ":tag #bad" \
    "${test_dir}/logfile_access_log.0"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":filter-expr :sc_bytes > 2000" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":filter-expr :sc_bytes # ff" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":goto 0" \
    -c ":close" \
    -c ":goto 0" \
    "${test_dir}/logfile_access_log.*"

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":goto 0" \
    -c ":hide-file" \
    ${test_dir}/logfile_access_log.*

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":goto 0" \
    -c ":next-mark error" \
    -c ":prev-location" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":goto 0" \
    -c ":next-mark error" \
    -c ":prev-location" \
    -c ":next-location" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":filter-in vmk" \
    -c ":disable-filter vmk" \
    -c ":goto 0" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":filter-in vmk" \
    -c ":rebuild" \
    -c ":reset-session" \
    -c ":rebuild" \
    -c ":goto 0" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":goto 0" \
    -c ":filter-out vmk" \
    -c ":toggle-filtering" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":hide-fields foobar" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":hide-fields cs_uri_stem" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":hide-fields access_log.c_ip access_log.cs_uri_stem" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ':hide-fields log_time log_level' \
    ${test_dir}/logfile_generic.0

run_cap_test ${lnav_test} -f- -n < ${test_dir}/formats/scripts/multiline-echo.lnav

run_cap_test ${lnav_test} -n \
    -c ":config /bad/option" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -nvq \
    -c ":config /ui/clock-format" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -nv \
    -c ":config /ui/clock-format" \
    -c ":config /ui/clock-format abc" \
    -c ":config /ui/clock-format" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -nv \
    -c ":config /ui/clock-format abc" \
    -c ":reset-config /ui/clock-format" \
    -c ":config /ui/clock-format" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c "|${test_dir}/toplevel.lnav 123 456 789" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -f "nonexistent.lnav" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":adjust-log-time 2010-01-01T00:00:00" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":adjust-log-time -1h" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ':goto 2022-06-16Tabc' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ':goto 17:00:01.' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto -1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":goto 2 hours later" \
    ${test_dir}/logfile_syslog_with_mixed_times.0

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":goto 3:45" \
    ${test_dir}/logfile_syslog_with_mixed_times.0

run_cap_test ${lnav_test} -n \
    -c ":goto invalid" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":relative-goto -1" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":next-mark error" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto -1" \
    -c ":prev-mark error" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":next-mark foobar" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":filter-in vmk" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":filter-in vmk" \
    -c ":reset-session" \
    -c ":filter-in cgi" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":filter-in today" \
    ${test_dir}/logfile_multiline.0

run_cap_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":filter-out today" \
    ${test_dir}/logfile_multiline.0

cp ${test_dir}/logfile_multiline.0 logfile_append.0
chmod ug+w logfile_append.0

run_cap_test ${lnav_test} -n \
    -c ";update generic_log set log_mark=1" \
    -c ":filter-in Goodbye" \
    -c ":append-to logfile_append.0" \
    -c ":rebuild" \
    logfile_append.0

cp ${test_dir}/logfile_multiline.0 logfile_append.0
chmod ug+w logfile_append.0

run_cap_test ${lnav_test} -n -d /tmp/lnav-search.err \
    -c "/goodbye" \
    -c ";update generic_log set log_mark=1" \
    -c ":filter-in Goodbye" \
    -c ":append-to logfile_append.0" \
    -c ":rebuild" \
    -c ":next-mark search" \
    logfile_append.0

cp ${test_dir}/logfile_multiline.0 logfile_append.0
chmod ug+w logfile_append.0

run_cap_test ${lnav_test} -n \
    -c ":filter-out Goodbye" \
    -c ":shexec echo '2009-07-20 22:59:30,221:ERROR:Goodbye, World!' >> logfile_append.0" \
    -c ":rebuild" \
    logfile_append.0

run_cap_test ${lnav_test} -n \
    -c ":filter-in avahi" \
    -c ":delete-filter avahi" \
    -c ":filter-in avahi" \
    -c ":filter-in dnsmasq" \
    ${test_dir}/logfile_filter.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view text" \
    -c ":filter-in World" \
    ${test_dir}/logfile_plain.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view text" \
    -c ":filter-out World" \
    ${test_dir}/logfile_plain.0

TOO_MANY_FILTERS=""
for i in `seq 1 32`; do
    TOO_MANY_FILTERS="$TOO_MANY_FILTERS -c ':filter-out $i'"
done
run_cap_test eval ${lnav_test} -d /tmp/lnav.err -n \
    $TOO_MANY_FILTERS \
    ${test_dir}/logfile_filter.0

run_cap_test ${lnav_test} -n \
    -c ":close" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":close" \
    -c ":close" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":open" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":close" \
    -c ":open ${test_dir}/logfile_multiline.0" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":close" \
    -c ":open /non-existent" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":goto 1" \
    -c ":write-screen-to -" \
    "${test_dir}/logfile_access_log.0"

run_cap_test ${lnav_test} -n \
    -c ";select * from access_log" \
    -c ':write-json-to -' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ";select * from access_log" \
    -c ':write-jsonlines-to -' \
    ${test_dir}/logfile_access_log.0

# By setting the LNAVSECURE mode before executing the command, we will disable
# the access to the write-json-to command and the output would just be the
# actual display of select query rather than json output.
export LNAVSECURE=1
run_cap_test env TEST_COMMENT="secure mode write test" ${lnav_test} -n \
    -c ";select * from access_log" \
    -c ':write-json-to /tmp/bad' \
    ${test_dir}/logfile_access_log.0

unset LNAVSECURE

run_cap_test ${lnav_test} -n \
    -c ";update generic_log set log_mark=1" \
    -c ":pipe-to sed -e 's/World!/Bork!/g' -e 's/2009//g'" \
    ${test_dir}/logfile_multiline.0

run_cap_test ${lnav_test} -n \
    -c ":echo Hello, World!" \
    -c ":goto 2" \
    -c ":pipe-line-to sed -e 's/World!/Bork!/g' -e 's/2009//g'" \
    ${test_dir}/logfile_multiline.0

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":pipe-line-to xargs echo \$cs_uri_stem \$sc_status - " \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 5" \
    -c ":pipe-line-to xargs echo \$log_raw_text \$log_level \$user - " \
    ${test_dir}/logfile_json.json

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/textfile_json_one_line.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/textfile_json_one_line.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/textfile_quoted_json.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_vami.0

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_pretty.0

run_cap_test ${lnav_test} -n \
    -c ":set-min-log-level error" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":highlight foobar" \
    -c ":clear-highlight foobar" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":clear-highlight foobar" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":zoom-to 4-hour" \
    ${test_dir}/textfile_json_indented.0

cp ${test_dir}/logfile_rollover.1 logfile_rollover.1.live
chmod ug+w logfile_rollover.1.live
touch -t 200711030923 logfile_rollover.1.live

run_cap_test ${lnav_test} -n \
    -c ":shexec echo 'Jan  3 09:23:38 veridian automount[16442]: attempting to mount entry /auto/opt' >> logfile_rollover.1.live" \
    -c ":rebuild" \
    -c ":switch-to-view histogram" \
    -c ":goto 0" \
    logfile_rollover.1.live

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":goto next year" \
    logfile_rollover.1.live

touch -t 200711030923 ${srcdir}/logfile_syslog.0
run_cap_test ${lnav_test} -n \
    -c ":switch-to-view histogram" \
    -c ":zoom-to 4-hour" \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view histogram" \
    -c ":zoom-to 1-day" \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ":filter-in sudo" \
    -c ":switch-to-view histogram" \
    -c ":zoom-to 4-hour" \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ":mark-expr" \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ":mark-expr :log_procname lik" \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ":mark-expr :cs_uri_stem LIKE '%vmk%'" \
    -c ":write-to -" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":mark-expr :log_body LIKE '%service%'" \
    ${test_dir}/logfile_json2.json

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":mark" \
    -c ":switch-to-view histogram" \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -n \
    -c ":zoom-to bad" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -f ${test_dir}/multiline.lnav \
    ${test_dir}/logfile_access_log.0

printf "Hello, World!" | run_cap_test env TEST_COMMENT="text view" ${lnav_test} -n \
  -c ":switch-to-view text"

run_cap_test ${lnav_test} -Nnv \
    -c ":hide-lines-before 2009-07-20T22:59:29" \
    -c ":hide-lines-before"

run_cap_test ${lnav_test} -Nnv \
    -c ":hide-lines-after 2009-07-20T22:59:29" \
    -c ":hide-lines-after"

run_cap_test ${lnav_test} -Nnv \
    -c ":hide-lines-before 2009-07-20T22:00:29" \
    -c ":hide-lines-after 2009-07-20T22:59:29" \
    -c ":hide-lines-before"

run_cap_test ${lnav_test} -n \
    -c ":hide-lines-before 2009-07-20T22:59:29" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":hide-lines-after 2009-07-20T22:59:26" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":hide-lines-after 2009-07-20T22:59:26" \
    -c ":show-lines-before-and-after" \
    ${test_dir}/logfile_access_log.0

export XYZ="World"

run_cap_test ${lnav_test} -n \
    -c ':echo Hello, \$XYZ!' \
    ${test_dir}/logfile_access_log.0

export XYZ="World"

run_cap_test ${lnav_test} -n \
    -c ':echo -n Hello, ' \
    -c ':echo World!' \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ':echo Hello, $XYZ!' \
    ${test_dir}/logfile_access_log.0
