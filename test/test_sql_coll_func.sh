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

# An ordering has to be consistent: no three values can have a < b < c < a.
# Something that is not an address sorts before addresses, whether it is
# made of digits or too long to be one.
run_cap_test ./drive_sql "SELECT ('zzz' < '1.2.3.4' COLLATE ipaddress) AND ('1.2.3.4' < '999' COLLATE ipaddress) AND ('999' < 'zzz' COLLATE ipaddress) AS cycle"

run_cap_test ./drive_sql "SELECT ('zzz' < '1.2.3.4' COLLATE ipaddress) AND ('1.2.3.4' < printf('%.130c', '9') COLLATE ipaddress) AND (printf('%.130c', '9') < 'zzz' COLLATE ipaddress) AS cycle"

run_cap_test ./drive_sql "SELECT ('01.2.3.5' < 'zzz' COLLATE ipaddress) AND ('zzz' < '1.2.3.4' COLLATE ipaddress) AND ('1.2.3.4' < '01.2.3.5' COLLATE ipaddress) AS cycle"

run_cap_test ./drive_sql "SELECT group_concat(v, ' ') AS sorted FROM (SELECT column1 AS v FROM (VALUES ('10.0.0.2'), ('host'), ('::1'), ('10.0.0.10'), ('-'), ('::ffff:10.0.0.3'), ('999'), ('0.0.0.0'), ('255.255.255.255')) ORDER BY v COLLATE ipaddress)"

run_cap_test ./drive_sql "select 'file10.txt' < 'file2.txt'"

run_cap_test ./drive_sql "select 'file10.txt' < 'file2.txt' collate naturalcase"

run_cap_test ./drive_sql "select 'w' < 'e' collate loglevel"

run_cap_test ./drive_sql "select 'e' < 'w' collate loglevel"

run_cap_test ./drive_sql "select 'info' collate loglevel between 'trace' and 'fatal'"

run_cap_test ./drive_sql "SELECT '10GB' < '1B' COLLATE measure_with_units"

# Text that is not a measurement sorts before all of the measurements.
run_cap_test ./drive_sql "SELECT ('10KB' < '9MB' COLLATE measure_with_units) AND ('9MB' < '9ZZ' COLLATE measure_with_units) AND ('9ZZ' < '10KB' COLLATE measure_with_units) AS cycle"

run_cap_test ./drive_sql "SELECT group_concat(v, ' ') AS sorted FROM (SELECT column1 AS v FROM (VALUES ('2MB'), ('-'), ('10KB'), ('abc'), ('1.5KB'), ('512')) ORDER BY v COLLATE measure_with_units)"
