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

run_cap_test ./drive_sql_anno "SELECT * FROM (SELECT foo, bar FROM baz ORDER BY foo DESC)"

run_cap_test ./drive_sql_anno \
   "SELECT * from vmw_log, regexp_capture(log_body, '--> /SessionStats/SessionPool/Session/(?<line>[abc]+)')"

run_cap_test ./drive_sql_anno "SELECT * FROM foo.bar"

run_cap_test ./drive_sql_anno 'SELECT * FROM tbl WHERE line = $LINE OR pid = :pid'

run_cap_test ./drive_sql_anno "SELECT json_object('abc', 'def') ->> '$.abc'"

run_cap_test ./drive_sql_anno "SELECT 0x77, 123, 123e4"

run_cap_test ./drive_sql_anno "REPLACE INTO tbl VALUES (1,'Leopard'),(2,'Dog')"

run_cap_test ./drive_sql_anno "INSERT INTO tbl VALUES (1,'Leopard') ON CONFLICT DO UPDATE SET foo=1"

run_cap_test ./drive_sql_anno "SELECT * FROM foo LEFT JOIN mycol ORDER BY blah"

run_cap_test ./drive_sql_anno "SELECT * FROM foo WHERE name = 'John' GROUP BY some_column HAVING column > 10 ORDER BY other_column"

run_cap_test ./drive_sql_anno "SELECT * FROM foo ORDER BY col1 ASC, col2 DESC"

read -r -d '' WITH_SQL_1 <<EOF
WITH
cte_1 AS (
  SELECT a FROM b WHERE c = 1
),
cte_2 AS (
  SELECT c FROM d WHERE e = 2
),
final AS (
  SELECT * FROM cte_1 LEFT JOIN cte_2 ON b = d
)
SELECT * FROM final;
EOF

run_cap_test ./drive_sql_anno "$WITH_SQL_1"

run_cap_test ./drive_sql_anno "CASE WHEN opt = 'foo' THEN 1 WHEN opt = 'bar' THEN 2 WHEN opt = 'baz' THEN 3 ELSE 4 END"

run_cap_test ./drive_sql_anno "CASE trim(sqrt(2)) WHEN 'one' THEN 1 WHEN 'two' THEN 2 WHEN 'three' THEN 3 ELSE 4 END;"

run_cap_test ./drive_sql_anno "CREATE TABLE tbl (a INT PRIMARY KEY, b TEXT, c INT NOT NULL, doggie INT NOT NULL)"

run_cap_test ./drive_sql_anno "SELECT RANK() OVER (PARTITION BY explosion ORDER BY day ROWS BETWEEN 6 PRECEDING AND CURRENT ROW) AS amount FROM tbl"

run_cap_test ./drive_sql_anno "DELETE FROM Customers WHERE CustomerName='Alfred' AND Phone=5002132"

run_cap_test ./drive_sql_anno "SELECT foo FROM bar UNION ALL SELECT foo FROM baz"

run_cap_test ./drive_sql_anno "SELECT foo FROM bar INTERSECT ALL SELECT foo FROM baz"

run_cap_test ./drive_sql_anno "SELECT foo FROM bar EXCEPT ALL SELECT foo FROM baz"

run_cap_test ./drive_sql_anno "UPDATE customers SET total_orders = order_summary.total  FROM ( SELECT * FROM bank) AS order_summary"

run_cap_test ./drive_sql_anno "SELECT x'aabb'"

run_cap_test ./drive_sql_anno "SELECT x'aab"

run_cap_test ./drive_sql_anno "from access_log | filter cs_method == 'GET' || cs_method == 'PUT'" 2

run_cap_test ./drive_sql_anno "from access_log | stats.count_by { c_ip }" 23

run_cap_test ./drive_sql_anno "from access_log | stats.count_by cs_uri_stem | take 10"
