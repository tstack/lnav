#! /bin/bash

export TZ="UTC"
export YES_COLOR=1

run_cap_test ${lnav_test} -h

run_cap_test ${lnav_test} badfilename

run_cap_test ${lnav_test} -n -c 'foo'

run_cap_test ${lnav_test} -d /tmp/lnav.err -n <<EOF
Hello, World!
Goodbye, World!
EOF

mkdir -p nested/sub1/sub2
echo "2021-07-03T21:49:29 Test" > nested/sub1/sub2/test.log

run_cap_test ${lnav_test} -nr nested

printf "a\ba _\ba a\b_" | run_cap_test env TEST_COMMENT="overstrike bold" \
    ${lnav_test} -n

{
  echo "This is the start of a file with long lines"
  ${lnav_test} -nN \
    -c ";select replicate('abcd', 2 * 1024 * 1024)" -c ':write-raw-to -'
  echo "abcd"
  echo "Goodbye"
} > textfile_long_lines.0

grep abcd textfile_long_lines.0 | run_cap_test \
    ${lnav_test} -n -d /tmp/lnav.err \
    -c ';SELECT filepath, lines FROM lnav_file'

export HOME="./piper-config"
rm -rf ./piper-config
mkdir -p $HOME/.lnav

${lnav_test} -Nn -c ':config /tuning/piper/max-size 128'

cat ${test_dir}/logfile_haproxy.0 | run_cap_test \
    env TEST_COMMENT="stdin rotation" ${lnav_test} -n

export HOME="./mgmt-config"
rm -rf ./mgmt-config
mkdir -p $HOME/.lnav
run_cap_test ${lnav_test} -m -I ${test_dir} config get

run_cap_test ${lnav_test} -m -I ${test_dir} config blame

export TMPDIR="piper-tmp"
rm -rf ./piper-tmp
mkdir piper-tmp
run_cap_test ${lnav_test} -n -e 'cat ${test_dir}/textfile_plain.0'

run_cap_test ${lnav_test} -n -e 'cat ${test_dir}/textfile_broken_gz.txt.gz'

run_cap_test ${lnav_test} -m piper list

PIPER_URL=$(env NO_COLOR=1 ${lnav_test} -m -q piper list | tail -1 | sed -r -e 's;.*(piper://[^ ]+).*;\1;g')

run_cap_test ${lnav_test} -n $PIPER_URL

run_cap_test ${lnav_test} -n $PIPER_URL \
    -c ";SELECT filepath, descriptor, mimetype, jget(content, '/ctime') as ctime, jget(content, '/cwd') as cwd FROM lnav_file_metadata" \
    -c ':write-json-to -'

run_cap_test ${lnav_test} -m \
    format access_log test non-existent

run_cap_test ${lnav_test} -m \
    format access_log test /tmp

run_cap_test ${lnav_test} -m \
    format access_log test ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -m \
    format access_log test ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -m \
    -I ${test_dir} \
    format test_log test ${test_dir}/logfile_json.json

run_cap_test ${lnav_test} -m \
    -I ${test_dir} \
    format test_log test ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -nN \
    -S "30m ago" \
    -U "40m ago"

run_cap_test ${lnav_test} -nN -S "abc"

run_cap_test ${lnav_test} -nN -S "2020-01-01abc"

run_cap_test ${lnav_test} -m file split

run_cap_test ${lnav_test} -m file split --size abc ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -m file split --time xyz ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -m file split ${test_dir}

rm -rf split-out
mkdir split-out

run_cap_test ${lnav_test} -m file split -o split-out \
    ${test_dir}/logfile_syslog.0

run_cap_test ${lnav_test} -m file split --lines 2 -o split-out \
    ${test_dir}/logfile_syslog.0

cat split-out/logfile_syslog.0.* > split-out.syslog.cat
run_cap_test cmp split-out.syslog.cat ${test_dir}/logfile_syslog.0

# refuses to overwrite the pieces from the previous run
run_cap_test env TEST_COMMENT="split overwrite" ${lnav_test} -m file split \
    --lines 2 -o split-out \
    ${test_dir}/logfile_syslog.0

# timestamps without a year cannot be split by time
run_cap_test ${lnav_test} -m file split --time 1m -o split-out \
    ${test_dir}/logfile_syslog.1

rm -rf split-time
mkdir split-time
run_cap_test ${lnav_test} -m file split --time 1m -o split-time \
    ${test_dir}/logfile_java.0

# every piece keeps the header lines needed to detect the format
rm -rf split-csv
mkdir split-csv
run_cap_test ${lnav_test} -m file split --lines 4 -o split-csv \
    ${test_dir}/logfile_win_events_csv.0

run_cap_test ${lnav_test} -n \
    -c ';SELECT log_format, count(*) FROM all_logs GROUP BY log_format' \
    split-csv/logfile_win_events_csv.0.0003

# compressed input is written out uncompressed
cp ${test_dir}/logfile_syslog.0 split-sys.log
gzip -f split-sys.log
rm -rf split-gz
mkdir split-gz
${lnav_test} -m file split --lines 2 -o split-gz split-sys.log.gz > /dev/null
cat split-gz/split-sys.0*.log > split-gz.cat
run_cap_test cmp split-gz.cat ${test_dir}/logfile_syslog.0

# nothing is written when the output directory is short on space
rm -rf split-space
mkdir split-space
${lnav_test} -nN -c ':config /tuning/archive-manager/min-free-space 1125899906842624'
${lnav_test} -m file split --lines 2 -o split-space \
    ${test_dir}/logfile_syslog.0 2> split-space.err
sed -e 's/only .* is available/only NNN is available/' \
    split-space.err > split-space.masked
run_cap_test cat split-space.masked
run_cap_test ls split-space
${lnav_test} -nN -c ':reset-config /tuning/archive-manager/min-free-space'

# JSON-lines: each piece starts with a whole JSON message
rm -rf split-json
mkdir split-json
run_cap_test ${lnav_test} -m file split --lines 3 -o split-json \
    ${test_dir}/logfile_bunyan.0

cat split-json/logfile_bunyan.0.* > split-json.cat
run_cap_test cmp split-json.cat ${test_dir}/logfile_bunyan.0

run_cap_test ${lnav_test} -n \
    -c ';SELECT log_format, count(*) FROM all_logs GROUP BY log_format' \
    split-json/logfile_bunyan.0.0004

# only the messages in a time range are written
rm -rf split-since
mkdir split-since
run_cap_test ${lnav_test} -m file split --since 2023-03-24T14:26:17Z \
    --lines 2 -o split-since ${test_dir}/logfile_bunyan.0

cat split-since/logfile_bunyan.0.* > split-since.cat
tail -5 ${test_dir}/logfile_bunyan.0 > split-since.expected
run_cap_test cmp split-since.cat split-since.expected

rm -rf split-until
mkdir split-until
run_cap_test ${lnav_test} -m file split --until 2023-03-24T14:26:17Z \
    -o split-until ${test_dir}/logfile_bunyan.0

cat split-until/logfile_bunyan.0.* > split-until.cat
head -5 ${test_dir}/logfile_bunyan.0 > split-until.expected
run_cap_test cmp split-until.cat split-until.expected

# the short flags match the main command line
rm -rf split-short-since split-short-until
mkdir split-short-since split-short-until
${lnav_test} -m file split -S 2023-03-24T14:26:17Z \
    -o split-short-since ${test_dir}/logfile_bunyan.0 > /dev/null
cat split-short-since/logfile_bunyan.0.* > split-short-since.cat
run_cap_test cmp split-short-since.cat split-since.expected

${lnav_test} -m file split -U 2023-03-24T14:26:17Z \
    -o split-short-until ${test_dir}/logfile_bunyan.0 > /dev/null
cat split-short-until/logfile_bunyan.0.* > split-short-until.cat
run_cap_test cmp split-short-until.cat split-until.expected

rm -rf split-empty
mkdir split-empty
run_cap_test ${lnav_test} -m file split --since 2030-01-01 \
    -o split-empty ${test_dir}/logfile_bunyan.0

run_cap_test ${lnav_test} -m file split --since 2023-03-25 \
    --until 2023-03-24 ${test_dir}/logfile_bunyan.0

run_cap_test ${lnav_test} -m file split --since bogus \
    ${test_dir}/logfile_bunyan.0

# the header lines are still copied when the range skips past them
rm -rf split-csv-range
mkdir split-csv-range
run_cap_test ${lnav_test} -m file split --since 2018-10-22T04:14:00 \
    --lines 3 -o split-csv-range ${test_dir}/logfile_win_events_csv.0

run_cap_test ${lnav_test} -n \
    -c ';SELECT log_format, count(*) FROM all_logs GROUP BY log_format' \
    split-csv-range/logfile_win_events_csv.0.0002

# The header lines have no time, so they are skipped along with the messages
# before the range.  The start of the range is past the first batch that is
# indexed and the file has no final newline, so the index does not skip ahead
# on its own.
awk 'BEGIN {
    print "#TYPE Selected.System.Diagnostics.Eventing.Reader.EventLogRecord";
    printf "\"TimeCreated\",\"ProviderName\",\"Message\"";
    for (i = 0; i < 150000; i++) {
        s = i % 60; m = int(i / 60) % 60; h = int(i / 3600) % 24;
        h12 = h % 12; if (h12 == 0) { h12 = 12 }
        printf "\n\"10/%02d/2018 %d:%02d:%02d %s\",\"docker\",\"msg %d\"",
            22 + int(i / 86400), h12, m, s, (h < 12 ? "AM" : "PM"), i;
    }
}' > split-big.csv
rm -rf split-big-csv
mkdir split-big-csv
run_cap_test ${lnav_test} -m file split --since 2018-10-23T09:20:00 \
    --lines 40000 -o split-big-csv split-big.csv

cat split-big-csv/split-big.0*.csv > split-big-csv.cat
{ head -2 split-big.csv; tail -n 30000 split-big.csv; } > split-big-csv.expected
run_cap_test cmp split-big-csv.cat split-big-csv.expected

# A leading line without a timestamp gets the time of the first message, so
# it is before the range as well.
awk 'BEGIN {
    printf "a line without a timestamp";
    for (i = 0; i < 150000; i++) {
        s = i % 60; m = int(i / 60) % 60; h = int(i / 3600) % 24;
        printf "\n2018-10-%02d %02d:%02d:%02d,000 INFO msg %d",
            22 + int(i / 86400), h, m, s, i;
    }
}' > split-big.log
rm -rf split-big-log
mkdir split-big-log
run_cap_test ${lnav_test} -m file split --since 2018-10-23T09:20:00 \
    --lines 40000 -o split-big-log split-big.log

cat split-big-log/split-big.0*.log > split-big-log.cat
tail -n 30000 split-big.log > split-big-log.expected
run_cap_test cmp split-big-log.cat split-big-log.expected

# The December messages are moved back a year when the January ones are read,
# after the first piece has been written.  Opened by itself, that piece should
# still get the year from its modification time.
awk 'BEGIN {
    for (i = 0; i < 120000; i++) {
        t = i * 8;
        printf "Dec %2d %02d:%02d:%02d host app[1]: msg %d\n",
            20 + int(t / 86400), int(t / 3600) % 24, int(t / 60) % 60, t % 60, i;
    }
    for (j = 0; j < 30000; j++) {
        t = j * 8;
        printf "Jan %2d %02d:%02d:%02d host app[1]: msg %d\n",
            1 + int(t / 86400), int(t / 3600) % 24, int(t / 60) % 60, t % 60,
            120000 + j;
    }
}' > split-rollover.log
touch -t 202601040000 split-rollover.log
rm -rf split-rollover
mkdir split-rollover
run_cap_test ${lnav_test} -m file split --lines 50000 -o split-rollover \
    split-rollover.log

run_cap_test ${lnav_test} -n \
    -c ';SELECT min(log_time), max(log_time) FROM all_logs' \
    split-rollover/split-rollover.0001.log
