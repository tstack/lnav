#! /bin/bash

run_test ./drive_sql "select timeslice()"

check_error_output "timeslice()" <<EOF
error: sqlite3_exec failed -- wrong number of arguments to function timeslice()
EOF

run_test ./drive_sql "select timeslice(1)"

check_error_output "timeslice(1)" <<EOF
error: sqlite3_exec failed -- wrong number of arguments to function timeslice()
EOF

run_test ./drive_sql "select timeslice('', '')"

check_error_output "timeslice empty" <<EOF
error: sqlite3_exec failed -- no time slice value given
EOF

run_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '8 am')"

check_error_output "timeslice abs" <<EOF
error: sqlite3_exec failed -- absolute time slices are not valid
EOF

run_test ./drive_sql "select timeslice(null, null)"

check_output "timeslice(null, null)" <<EOF
Row 0:
  Column timeslice(null, null): (null)
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
