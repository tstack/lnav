#! /bin/sh

run_test ./drive_line_buffer ${top_srcdir}/src/line_buffer.hh

check_output "Line buffer output doesn't match input?" < \
    ${top_srcdir}/src/line_buffer.hh

run_test ./drive_line_buffer < ${top_srcdir}/src/line_buffer.hh

check_output "Line buffer output doesn't match input from pipe?" < \
    ${top_srcdir}/src/line_buffer.hh

run_test ./drive_line_buffer -d " " ${top_srcdir}/src/line_buffer.hh

check_output "Line buffer with delim output doesn't match input?" < \
    ${top_srcdir}/src/line_buffer.hh

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

cat ${top_srcdir}/src/*.hh ${top_srcdir}/src/*.cc > lb-2.dat
grep -b '$' lb-2.dat | cut -f 1 -d : > lb.index
line_count=`wc -l lb-2.dat`

run_test ./drive_line_buffer -i lb.index -n 100 lb-2.dat

check_output "Random reads don't match input?" <<EOF
All done
EOF
