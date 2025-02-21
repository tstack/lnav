#! /bin/bash

run_cap_test ./drive_sql_anno ".dump /foo/bar abc"

# basic query
run_cap_test ./drive_sql_anno "SELECT * FROM FOO"

# no help for keyword flag
run_cap_test ./drive_sql_anno "TABLE"

# nested function calls
run_cap_test ./drive_sql_anno "SELECT foo(bar())"

# nested function calls
run_cap_test ./drive_sql_anno "SELECT foo(bar())" 2

# caret in keyword whitespace
run_cap_test ./drive_sql_anno "SELECT       lower(abc)" 9

# caret in function whitespace
run_cap_test ./drive_sql_anno "SELECT lower(   abc    )" 14

# caret in unfinished function call
run_cap_test ./drive_sql_anno "SELECT lower(abc" 16

# caret on the outer function
run_cap_test ./drive_sql_anno "SELECT instr(lower(abc), '123')" 9

# caret on a nested function
run_cap_test ./drive_sql_anno "SELECT instr(lower(abc), '123')" 15

# caret on a flag
run_cap_test ./drive_sql_anno "SELECT instr(lower(abc), '123') FROM bar" 30

# multiple help hits
run_cap_test ./drive_sql_anno "CREATE" 2

# string vs ident
run_cap_test ./drive_sql_anno "SELECT 'hello, world!' FROM \"my table\""

# math
run_cap_test ./drive_sql_anno "SELECT (1 + 2) AS three"

run_cap_test ./drive_sql_anno "SELECT (1.5 + 2.2) AS decim"

# subqueries
run_cap_test ./drive_sql_anno "SELECT * FROM (SELECT foo, bar FROM baz)"

run_cap_test ./drive_sql_anno \
   "SELECT * from vmw_log, regexp_capture(log_body, '--> /SessionStats/SessionPool/Session/(?<line>[abc]+)')"

run_cap_test ./drive_sql_anno "SELECT * FROM foo.bar"

run_cap_test ./drive_sql_anno "SELECT json_object('abc', 'def') ->> '$.abc'"

run_cap_test ./drive_sql_anno "SELECT 0x77, 123, 123e4"

run_cap_test ./drive_sql_anno "from access_log | filter cs_method == 'GET' || cs_method == 'PUT'" 2

run_cap_test ./drive_sql_anno "from access_log | stats.count_by { c_ip }" 23

run_cap_test ./drive_sql_anno "from access_log | stats.count_by cs_uri_stem | take 10"
