#! /bin/bash

run_test ./drive_sql "select '192.168.1.10' < '192.168.1.2'"

check_output "" <<EOF
Row 0:
  Column '192.168.1.10' < '192.168.1.2': 1
EOF

run_test ./drive_sql "select '192.168.1.10' < '192.168.1.2' collate ipaddress"

check_output "" <<EOF
Row 0:
  Column '192.168.1.10' < '192.168.1.2' collate ipaddress: 0
EOF

run_test ./drive_sql "select '::ffff:192.168.1.10' = '192.168.1.10' collate ipaddress"

check_output "" <<EOF
Row 0:
  Column '::ffff:192.168.1.10' = '192.168.1.10' collate ipaddress: 1
EOF

run_test ./drive_sql "select 'fe80::a85f:80b4:5cbe:8691' = 'fe80:0000:0000:0000:a85f:80b4:5cbe:8691' collate ipaddress"

check_output "" <<EOF
Row 0:
  Column 'fe80::a85f:80b4:5cbe:8691' = 'fe80:0000:0000:0000:a85f:80b4:5cbe:8691' collate ipaddress: 1
EOF

run_test ./drive_sql "select '' < '192.168.1.2' collate ipaddress"

check_output "" <<EOF
Row 0:
  Column '' < '192.168.1.2' collate ipaddress: 1
EOF

run_test ./drive_sql "select '192.168.1.2' > '' collate ipaddress"

check_output "" <<EOF
Row 0:
  Column '192.168.1.2' > '' collate ipaddress: 1
EOF

run_test ./drive_sql "select '192.168.1.2' < 'fe80::a85f:80b4:5cbe:8691' collate ipaddress"

check_output "" <<EOF
Row 0:
  Column '192.168.1.2' < 'fe80::a85f:80b4:5cbe:8691' collate ipaddress: 1
EOF

run_test ./drive_sql "select 'h9.example.com' < 'h10.example.com' collate ipaddress"

check_output "" <<EOF
Row 0:
  Column 'h9.example.com' < 'h10.example.com' collate ipaddress: 1
EOF


run_test ./drive_sql "select 'file10.txt' < 'file2.txt'"

check_output "" <<EOF
Row 0:
  Column 'file10.txt' < 'file2.txt': 1
EOF

run_test ./drive_sql "select 'file10.txt' < 'file2.txt' collate naturalcase"

check_output "" <<EOF
Row 0:
  Column 'file10.txt' < 'file2.txt' collate naturalcase: 0
EOF


run_test ./drive_sql "select 'w' < 'e' collate loglevel"

check_output "" <<EOF
Row 0:
  Column 'w' < 'e' collate loglevel: 1
EOF

run_test ./drive_sql "select 'e' < 'w' collate loglevel"

check_output "" <<EOF
Row 0:
  Column 'e' < 'w' collate loglevel: 0
EOF

run_test ./drive_sql "select 'info' collate loglevel between 'trace' and 'fatal'"

check_output "" <<EOF
Row 0:
  Column 'info' collate loglevel between 'trace' and 'fatal': 1
EOF
