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

run_test ./drive_sql "select regexp_replace('test 1 2 3', '\\d+', 'N')"

check_output "" <<EOF
Row 0:
  Column regexp_replace('test 1 2 3', '\d+', 'N'): test N N N
EOF


run_test ./drive_sql "select extract('abc', 'abc')"

check_error_output "" <<EOF
error: sqlite3_exec failed -- regular expression does not have any captures
EOF

run_test ./drive_sql "select extract(null, 'abc')"

check_error_output "" <<EOF
error: sqlite3_exec failed -- no regexp
EOF

run_test ./drive_sql "select extract('abc', null)"

check_error_output "" <<EOF
error: sqlite3_exec failed -- no string
EOF

run_test ./drive_sql "select typeof(result), result from (select extract('(\d*)abc', 'abc') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): text
  Column     result:
EOF

run_test ./drive_sql "select typeof(result), result from (select extract('(\d*)abc(\d*)', 'abc') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): text
  Column     result: {"col_0":"","col_1":""}
EOF

run_test ./drive_sql "select typeof(result), result from (select extract('(\d+)', '123') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): integer
  Column     result: 123
EOF

run_test ./drive_sql "select typeof(result), result from (select extract('a(\d+\.\d+)a', 'a123.456a') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): real
  Column     result: 123.456
EOF

run_test ./drive_sql "select extract('foo=(?<foo>\w+); (\w+)', 'foo=abc; 123') as result"

check_output "" <<EOF
Row 0:
  Column     result: {"foo":"abc","col_0":123}
EOF

run_test ./drive_sql "select extract('foo=(?<foo>\w+); (\w+\.\w+)', 'foo=abc; 123.456') as result"

check_output "" <<EOF
Row 0:
  Column     result: {"foo":"abc","col_0":123.456}
EOF
