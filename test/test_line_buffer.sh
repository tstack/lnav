#! /bin/bash

cp ${test_dir}/logfile_access_log.1 logfile_changed.0
chmod u+w logfile_changed.0
run_test ${lnav_test} -n \
    -c ":rebuild" \
    -c ":shexec head -1 ${test_dir}/logfile_access_log.0 > logfile_changed.0" \
    -c ":rebuild" \
    logfile_changed.0

check_error_output "line buffer cache flush" <<EOF
EOF

check_output "line buffer cache flush is not working" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
EOF

run_test ./drive_line_buffer "${top_srcdir}/src/line_buffer.hh"

check_output "Line buffer output doesn't match input?" < \
    "${top_srcdir}/src/line_buffer.hh"

run_test ./drive_line_buffer < ${top_srcdir}/src/line_buffer.hh

check_output "Line buffer output doesn't match input from pipe?" < \
    "${top_srcdir}/src/line_buffer.hh"

cat > lb.dat <<EOF
1
2
3
4
5
EOF

LINE_OFF=`grep -b '4' lb.dat | cut -f 1 -d :`

run_test ./drive_line_buffer -o $LINE_OFF lb.dat

check_output "Seeking in the line buffer doesn't work?" <<EOF
4
5
EOF

run_test ./drive_line_buffer -o 4424 -c 1 ${srcdir}/UTF-8-test.txt

check_output "Invalid UTF is not scrubbed?" <<EOF
2.1.5  5 bytes (U-00200000):        "?????"                                       |
EOF

cat "${top_srcdir}/src/"*.hh "${top_srcdir}/src/"*.cc > lb-2.dat
grep -b '$' lb-2.dat | cut -f 1 -d : > lb.index

run_test ./drive_line_buffer -i lb.index -n 10 lb-2.dat

check_output "Random reads don't match input?" <<EOF
All done
EOF

gzip -c ${test_dir}/logfile_access_log.1 > lb-double.gz
gzip -c ${test_dir}/logfile_access_log.1 >> lb-double.gz
run_test ${lnav_test} -n lb-double.gz

gzip -dc lb-double.gz | \
    check_output "concatenated gzip files don't parse correctly"

> lb-3.gz
while test $(wc -c < lb-3.gz) -le 5000000 ; do
    cat lb-2.dat
done | gzip -c -1 > lb-3.gz
gzip -dc lb-3.gz > lb-3.dat
grep -b '$' lb-3.dat | cut -f 1 -d : > lb-3.index

run_test ./drive_line_buffer -i lb-3.index -n 10 lb-3.gz lb-3.dat

check_output "Random gzipped reads don't match input" <<EOF
All done
EOF