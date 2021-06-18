#! /bin/bash

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'blah')"

check_error_output "timeslice('blah')" <<EOF
error: sqlite3_exec failed -- unable to parse time slice value: blah -- Unrecognized input
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'before fri')"

check_output "before 12pm" <<EOF
Row 0:
  Column timeslice('2015-08-07 12:01:00', 'before fri'): (null)
EOF

run_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'after fri')"

check_output "not before 12pm" <<EOF
Row 0:
  Column timeslice('2015-08-07 11:59:00', 'after fri'): (null)
EOF

run_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'fri')"

check_output "not before 12pm" <<EOF
Row 0:
  Column timeslice('2015-08-07 11:59:00', 'fri'): 2015-08-07 00:00:00.000
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'before 12pm')"

check_output "before 12pm" <<EOF
Row 0:
  Column timeslice('2015-08-07 12:01:00', 'before 12pm'): (null)
EOF

run_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'before 12pm')"

check_output "not before 12pm" <<EOF
Row 0:
  Column timeslice('2015-08-07 11:59:00', 'before 12pm'): 2015-08-07 00:00:00.000
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'after 12pm')"

check_output "after 12pm" <<EOF
Row 0:
  Column timeslice('2015-08-07 12:01:00', 'after 12pm'): 2015-08-07 12:00:00.000
EOF

run_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'after 12pm')"

check_output "not after 12pm" <<EOF
Row 0:
  Column timeslice('2015-08-07 11:59:00', 'after 12pm'): (null)
EOF

run_test ./drive_sql "select timeslice()"

check_error_output "timeslice()" <<EOF
error: sqlite3_exec failed -- timeslice() expects between 1 and 2 arguments
EOF

run_test ./drive_sql "select timeslice('2015-02-01T05:10:00')"

check_output "timeslice('2015-02-01T05:10:00')" <<EOF
Row 0:
  Column timeslice('2015-02-01T05:10:00'): 2015-02-01 05:00:00.000
EOF

run_test ./drive_sql "select timeslice('', '')"

check_error_output "timeslice empty" <<EOF
error: sqlite3_exec failed -- no time slice value given
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '8 am')"

check_output "timeslice abs" <<EOF
Row 0:
  Column timeslice('2015-08-07 12:01:00', '8 am'): (null)
EOF

run_test ./drive_sql "select timeslice('2015-08-07 08:00:33', '8 am')"

check_output "timeslice abs" <<EOF
Row 0:
  Column timeslice('2015-08-07 08:00:33', '8 am'): 2015-08-07 08:00:00.000
EOF

run_test ./drive_sql "select timeslice('2015-08-07 08:01:33', '8 am')"

check_output "timeslice abs" <<EOF
Row 0:
  Column timeslice('2015-08-07 08:01:33', '8 am'): (null)
EOF

run_test ./drive_sql "select timeslice(null, null)"

check_output "timeslice(null, null)" <<EOF
Row 0:
  Column timeslice(null, null): (null)
EOF

run_test ./drive_sql "select timeslice(null)"

check_output "timeslice(null)" <<EOF
Row 0:
  Column timeslice(null): (null)
EOF

run_test ./drive_sql "select timeslice(1616300753.333, '100ms')"

check_output "100ms slice" <<EOF
Row 0:
  Column timeslice(1616300753.333, '100ms'): 2021-03-21 04:25:53.300
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '5m')"

check_output "timeslice 5m" <<EOF
Row 0:
  Column timeslice('2015-08-07 12:01:00', '5m'): 2015-08-07 12:00:00.000
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '1d')"

check_output "timeslice 1d" <<EOF
Row 0:
  Column timeslice('2015-08-07 12:01:00', '1d'): 2015-08-07 00:00:00.000
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '1 month')"

# XXX This is wrong...
check_output "timeslice 1 month" <<EOF
Row 0:
  Column timeslice('2015-08-07 12:01:00', '1 month'): 2015-08-03 00:00:00.000
EOF


run_test ./drive_sql "select timediff('2017-01-02T05:00:00.100', '2017-01-02T05:00:00.000')"

check_output "timeslice ms" <<EOF
Row 0:
  Column timediff('2017-01-02T05:00:00.100', '2017-01-02T05:00:00.000'): 0.1
EOF

run_test ./drive_sql "select timediff('today', 'yesterday')"

check_output "timeslice day" <<EOF
Row 0:
  Column timediff('today', 'yesterday'): 86400.0
EOF

run_test ./drive_sql "select timediff('foo', 'yesterday')"

check_output "timeslice day" <<EOF
Row 0:
  Column timediff('foo', 'yesterday'): (null)
EOF
