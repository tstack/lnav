#! /bin/bash

run_test ./drive_sql "SELECT * FROM xpath('/abc[', '<abc/>')"

check_error_output "invalid xpath not reported?" <<EOF
error: sqlite3_exec failed -- Invalid XPATH expression at offset 5: Unrecognized node test
EOF

run_test ./drive_sql "SELECT * FROM xpath('/abc', '<abc')"

check_error_output "invalid XML not reported?" <<EOF
error: sqlite3_exec failed -- Invalid XML document at offset 3: Error parsing start element tag
EOF

run_test ./drive_sql "SELECT * FROM xpath('/abc/def', '<abc/>')"

check_output "got unexpected results" <<EOF
EOF

run_test ./drive_sql "SELECT * FROM xpath('/abc/def[@a=\"b\"]', '<abc><def/><def a=\"b\">ghi</def></abc>')"

check_output "got unexpected results" <<EOF
Row 0:
  Column     result: <def a="b">ghi</def>

  Column  node_path: /abc/def[2]
  Column  node_attr: {"a":"b"}
  Column  node_text: ghi
EOF

run_test ./drive_sql "SELECT * FROM xpath('/abc/def', '<abc><def>Hello &gt;</def></abc>')"

check_output "got unexpected results" <<EOF
Row 0:
  Column     result: <def>Hello &gt;</def>

  Column  node_path: /abc/def
  Column  node_attr: {}
  Column  node_text: Hello >
EOF
