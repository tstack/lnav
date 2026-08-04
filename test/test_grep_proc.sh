#! /bin/bash

cat > gp.dat <<EOF
Hello, World!
Goodbye, World?
EOF

grep_slice() {
    ./drive_grep_proc "$1" "$2" | ./slicer "$2"
}

grep_capture() {
    ./drive_grep_proc "$1" "$2" 1>/dev/null
}

# With more than one pattern, the driver prints "<line>/<mask>" where the mask
# has a bit set for each pattern slot that matched the line.
grep_multi() {
    ./drive_grep_proc "$@"
}

run_test grep_slice 'Hello' gp.dat

check_output "grep_proc didn't find the right match?" <<EOF
Hello
EOF

run_test grep_slice '.*' gp.dat

check_output "grep_proc didn't find all lines?" <<EOF
Hello, World!


Goodbye, World?


EOF

run_test grep_slice '\w+,' gp.dat

check_output "grep_proc didn't find the right matches?" <<EOF
Hello,
Goodbye,
EOF

run_test grep_slice '\w+.' gp.dat

check_output "grep_proc didn't find multiple matches?" <<EOF
Hello,
World!
Goodbye,
World?
EOF

run_test grep_capture '(\w+), World' gp.dat

check_error_output "grep_proc didn't capture matches?" <<EOF
0(0:5)Hello
1(0:7)Goodbye
EOF

check_output "grep_proc didn't capture matches?" <<EOF
EOF

# Each pattern lands in its own slot and a single pass reports every slot that
# matched, so a line hit by two patterns reports both bits.
run_test grep_multi 'Hello' 'Goodbye' gp.dat

check_output "grep_proc didn't keep the patterns in separate slots?" <<EOF
0/1
1/2
EOF

run_test grep_multi 'Hello' 'World' 'Goodbye' gp.dat

check_output "grep_proc didn't report every matching pattern?" <<EOF
0/3
1/6
EOF

# A pattern that matches nothing just never sets its bit.
run_test grep_multi 'Hello' 'nothing-matches-this' gp.dat

check_output "grep_proc reported a bit for a pattern that never matched?" <<EOF
0/1
EOF
