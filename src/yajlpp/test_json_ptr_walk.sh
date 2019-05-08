#! /bin/bash

run_test ./drive_json_ptr_walk <<EOF
{ "foo" : 1 }
EOF

check_output "simple object" <<EOF
/foo = 1
EOF

run_test ./drive_json_ptr_walk <<EOF
{ "~tstack/julia" : 1 }
EOF

check_output "escaped object" <<EOF
/~0tstack~1julia = 1
EOF

run_test ./drive_json_ptr_walk <<EOF
1
EOF

check_output "root value" <<EOF
 = 1
EOF

run_test ./drive_json_ptr_walk <<EOF
[1, 2, 3]
EOF

check_output "array" <<EOF
/0 = 1
/1 = 2
/2 = 3
EOF

run_test ./drive_json_ptr_walk <<EOF
[1, 2, 3, [4, 5, 6], 7, 8, 9, [10, 11, [12, 13, 14], 15], 16]
EOF

check_output "nested array" <<EOF
/0 = 1
/1 = 2
/2 = 3
/3/0 = 4
/3/1 = 5
/3/2 = 6
/4 = 7
/5 = 8
/6 = 9
/7/0 = 10
/7/1 = 11
/7/2/0 = 12
/7/2/1 = 13
/7/2/2 = 14
/7/3 = 15
/8 = 16
EOF

run_test ./drive_json_ptr_walk <<EOF
[null, true, 123.0, "foo", { "bar" : { "baz" : [1, 2, 3]} }, ["a", null]]
EOF

check_error_output "" <<EOF
EOF

check_output "complex" <<EOF
/0 = null
/1 = true
/2 = 123.0
/3 = "foo"
/4/bar/baz/0 = 1
/4/bar/baz/1 = 2
/4/bar/baz/2 = 3
/5/0 = "a"
/5/1 = null
EOF
