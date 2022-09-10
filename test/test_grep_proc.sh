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
