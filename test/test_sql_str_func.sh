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

run_test ./drive_sql "select regexp('abcd', 'abcd')"

check_output "" <<EOF
Row 0:
  Column regexp('abcd', 'abcd'): 1
EOF

run_test ./drive_sql "select regexp('bc', 'abcd')"

check_output "" <<EOF
Row 0:
  Column regexp('bc', 'abcd'): 1
EOF

run_test ./drive_sql "select regexp('[e-z]+', 'abcd')"

check_output "" <<EOF
Row 0:
  Column regexp('[e-z]+', 'abcd'): 0
EOF

run_test ./drive_sql "select regexp('[e-z]+', 'ea')"

check_output "" <<EOF
Row 0:
  Column regexp('[e-z]+', 'ea'): 1
EOF

run_test ./drive_sql "select regexp_replace('\\d+', 'test 1 2 3', 'N')"

check_output "" <<EOF
Row 0:
  Column regexp_replace('\d+', 'test 1 2 3', 'N'): test N N N
EOF
