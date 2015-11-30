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

run_test ./drive_sql "select json_group_object(key) from (select 1 as key)"

check_error_output "" <<EOF
error: sqlite3_exec failed -- Uneven number of arguments to json_group_object(), expecting key and value pairs
EOF

GROUP_SELECT_1=$(cat <<EOF
SELECT id, json_group_object(key, value) as stack FROM (
              SELECT 1 as id, 'key1' as key, 10 as value
    UNION ALL SELECT 1 as id, 'key2' as key, 20 as value
    UNION ALL SELECT 1 as id, 'key3' as key, 30 as value)
EOF
)

run_test ./drive_sql "$GROUP_SELECT_1"

check_error_output "" <<EOF
EOF

check_output "json_group_object does not work" <<EOF
Row 0:
  Column         id: 1
  Column      stack: {"key1":10,"key2":20,"key3":30}
EOF

GROUP_SELECT_2=$(cat <<EOF
SELECT id, json_group_object(key, value) as stack FROM (
              SELECT 1 as id, 1 as key, 10 as value
    UNION ALL SELECT 1 as id, 2 as key, null as value
    UNION ALL SELECT 1 as id, 3 as key, 30.5 as value)
EOF
)

run_test ./drive_sql "$GROUP_SELECT_2"

check_error_output "" <<EOF
EOF

check_output "json_group_object does not work" <<EOF
Row 0:
  Column         id: 1
  Column      stack: {"1":10,"2":null,"3":30.5}
EOF
