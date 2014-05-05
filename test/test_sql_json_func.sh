#! /bin/bash

run_test ./drive_sql "select jget('4', '')"

check_output "jget root does not work" <<EOF
Row 0:
  Column jget('4', ''): 4
EOF

run_test ./drive_sql "select jget('4', null)"

check_output "jget null does not work" <<EOF
Row 0:
  Column jget('4', null): 4
EOF

run_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/3')"

check_error_output "" <<EOF
EOF

check_output "jget null does not work" <<EOF
Row 0:
  Column jget('[null, true, 20, 30, 40]', '/3'): 30
EOF

run_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/abc')"

check_error_output "" <<EOF
EOF

check_output "jget for array does not work" <<EOF
Row 0:
  Column jget('[null, true, 20, 30, 40]', '/abc'): (null)
EOF

run_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/abc', 1)"

check_error_output "" <<EOF
EOF

check_output "jget for array does not work" <<EOF
Row 0:
  Column jget('[null, true, 20, 30, 40]', '/abc', 1): 1
EOF

run_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/0/foo')"

check_error_output "" <<EOF
EOF

check_output "jget for array does not work" <<EOF
Row 0:
  Column jget('[null, true, 20, 30, 40]', '/0/foo'): (null)
EOF
