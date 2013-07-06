#! /bin/bash

run_test ./drive_sql "select startswith('.foo', '.')"

check_output "" <<EOF
Row 0:
  Column startswith('.foo', '.'): 1
EOF

run_test ./drive_sql "select startswith('foo', '.')"

check_output "" <<EOF
Row 0:
  Column startswith('foo', '.'): 0
EOF

run_test ./drive_sql "select endswith('foo', '.')"

check_output "" <<EOF
Row 0:
  Column endswith('foo', '.'): 0
EOF

run_test ./drive_sql "select endswith('foo.', '.')"

check_output "" <<EOF
Row 0:
  Column endswith('foo.', '.'): 1
EOF

run_test ./drive_sql "select endswith('foo.txt', '.txt')"

check_output "" <<EOF
Row 0:
  Column endswith('foo.txt', '.txt'): 1
EOF

run_test ./drive_sql "select endswith('a', '.txt')"

check_output "" <<EOF
Row 0:
  Column endswith('a', '.txt'): 0
EOF
