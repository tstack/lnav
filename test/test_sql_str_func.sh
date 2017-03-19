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


run_test ./drive_sql "select regexp_match('abc', 'abc')"

check_error_output "" <<EOF
error: sqlite3_exec failed -- regular expression does not have any captures
EOF

run_test ./drive_sql "select regexp_match(null, 'abc')"

check_output "" <<EOF
Row 0:
  Column regexp_match(null, 'abc'): (null)
EOF

run_test ./drive_sql "select regexp_match('abc', null) as result"

check_output "" <<EOF
Row 0:
  Column     result: (null)
EOF

run_test ./drive_sql "select typeof(result), result from (select regexp_match('(\d*)abc', 'abc') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): text
  Column     result:
EOF

run_test ./drive_sql "select typeof(result), result from (select regexp_match('(\d*)abc(\d*)', 'abc') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): text
  Column     result: {"col_0":"","col_1":""}
EOF

run_test ./drive_sql "select typeof(result), result from (select regexp_match('(\d+)', '123') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): integer
  Column     result: 123
EOF

run_test ./drive_sql "select typeof(result), result from (select regexp_match('a(\d+\.\d+)a', 'a123.456a') as result)"

check_output "" <<EOF
Row 0:
  Column typeof(result): real
  Column     result: 123.456
EOF

run_test ./drive_sql "select regexp_match('foo=(?<foo>\w+); (\w+)', 'foo=abc; 123') as result"

check_output "" <<EOF
Row 0:
  Column     result: {"foo":"abc","col_0":123}
EOF

run_test ./drive_sql "select regexp_match('foo=(?<foo>\w+); (\w+\.\w+)', 'foo=abc; 123.456') as result"

check_output "" <<EOF
Row 0:
  Column     result: {"foo":"abc","col_0":123.456}
EOF


run_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '\w+ (\w+)')"

check_output "" <<EOF
Row 0:
  Column match_index: 0
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 2
  Column range_start: 0
  Column range_stop: 7
  Column    content: foo bar
Row 1:
  Column match_index: 0
  Column capture_index: 1
  Column capture_name:
  Column capture_count: 2
  Column range_start: 4
  Column range_stop: 7
  Column    content: bar
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '\w+ \w+')"

check_output "" <<EOF
Row 0:
  Column match_index: 0
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 1
  Column range_start: 0
  Column range_stop: 7
  Column    content: foo bar
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '\w+ (?<word>\w+)')"

check_output "" <<EOF
Row 0:
  Column match_index: 0
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 2
  Column range_start: 0
  Column range_stop: 7
  Column    content: foo bar
Row 1:
  Column match_index: 0
  Column capture_index: 1
  Column capture_name: word
  Column capture_count: 2
  Column range_start: 4
  Column range_stop: 7
  Column    content: bar
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '(bar)|\w+ (?<word>\w+)')"

check_output "" <<EOF
Row 0:
  Column match_index: 0
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 3
  Column range_start: 0
  Column range_stop: 7
  Column    content: foo bar
Row 1:
  Column match_index: 0
  Column capture_index: 1
  Column capture_name:
  Column capture_count: 3
  Column range_start: -1
  Column range_stop: -1
  Column    content: (null)
Row 2:
  Column match_index: 0
  Column capture_index: 2
  Column capture_name: word
  Column capture_count: 3
  Column range_start: 4
  Column range_stop: 7
  Column    content: bar
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture()"

check_output "" <<EOF
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture('foo bar')"

check_output "" <<EOF
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '(')"

check_error_output "" <<EOF
error: sqlite3_exec failed -- Invalid regular expression: missing )
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture('1 2 3 45', '(\d+)')"

check_output "" <<EOF
Row 0:
  Column match_index: 0
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 2
  Column range_start: 0
  Column range_stop: 1
  Column    content: 1
Row 1:
  Column match_index: 0
  Column capture_index: 1
  Column capture_name:
  Column capture_count: 2
  Column range_start: 0
  Column range_stop: 1
  Column    content: 1
Row 2:
  Column match_index: 1
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 2
  Column range_start: 2
  Column range_stop: 3
  Column    content: 2
Row 3:
  Column match_index: 1
  Column capture_index: 1
  Column capture_name:
  Column capture_count: 2
  Column range_start: 2
  Column range_stop: 3
  Column    content: 2
Row 4:
  Column match_index: 2
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 2
  Column range_start: 4
  Column range_stop: 5
  Column    content: 3
Row 5:
  Column match_index: 2
  Column capture_index: 1
  Column capture_name:
  Column capture_count: 2
  Column range_start: 4
  Column range_stop: 5
  Column    content: 3
Row 6:
  Column match_index: 3
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 2
  Column range_start: 6
  Column range_stop: 8
  Column    content: 45
Row 7:
  Column match_index: 3
  Column capture_index: 1
  Column capture_name:
  Column capture_count: 2
  Column range_start: 6
  Column range_stop: 8
  Column    content: 45
EOF

run_test ./drive_sql "SELECT * FROM regexp_capture('foo foo', '^foo')"

check_output "" <<EOF
Row 0:
  Column match_index: 0
  Column capture_index: 0
  Column capture_name: (null)
  Column capture_count: 1
  Column range_start: 0
  Column range_stop: 3
  Column    content: foo
EOF
