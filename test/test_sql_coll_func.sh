#! /bin/bash

run_cap_test ./drive_sql "select '192.168.1.10' < '192.168.1.2'"

run_cap_test ./drive_sql "select '192.168.1.10' < '192.168.1.2' collate ipaddress"

run_cap_test ./drive_sql "select '192.168.1.10' < '192.168.1.12' collate ipaddress"

run_cap_test ./drive_sql "select '::ffff:192.168.1.10' = '192.168.1.10' collate ipaddress"

run_cap_test ./drive_sql "select 'fe80::a85f:80b4:5cbe:8691' = 'fe80:0000:0000:0000:a85f:80b4:5cbe:8691' collate ipaddress"

run_cap_test ./drive_sql "select '' < '192.168.1.2' collate ipaddress"

run_cap_test ./drive_sql "select '192.168.1.2' > '' collate ipaddress"

run_cap_test ./drive_sql "select '192.168.1.2' < 'fe80::a85f:80b4:5cbe:8691' collate ipaddress"

run_cap_test ./drive_sql "select 'h9.example.com' < 'h10.example.com' collate ipaddress"

run_cap_test ./drive_sql "select 'file10.txt' < 'file2.txt'"

run_cap_test ./drive_sql "select 'file10.txt' < 'file2.txt' collate naturalcase"

run_cap_test ./drive_sql "select 'w' < 'e' collate loglevel"

run_cap_test ./drive_sql "select 'e' < 'w' collate loglevel"

run_cap_test ./drive_sql "select 'info' collate loglevel between 'trace' and 'fatal'"

run_cap_test ./drive_sql "SELECT '10GB' < '1B' COLLATE measure_with_units"
