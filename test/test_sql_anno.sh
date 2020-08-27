#! /bin/bash

run_test ./drive_sql_anno "SELECT * FROM FOO"

check_output "basic query" <<EOF
                 SELECT * FROM FOO
     sql_keyword ------
        sql_oper        -
     sql_keyword          ----
       sql_ident               ---
EOF

run_test ./drive_sql_anno "TABLE"

check_output "no help for keyword flag" <<EOF
                 TABLE
     sql_keyword -----
EOF

run_test ./drive_sql_anno "SELECT foo(bar())"

check_output "nested function calls" <<EOF
                 SELECT foo(bar())
     sql_keyword ------
       sql_ident        ---
        sql_func        ---------
       sql_ident            ---
        sql_func            ----
EOF

run_test ./drive_sql_anno "SELECT foo(bar())" 2

check_output "nested function calls" <<EOF
                 SELECT foo(bar())
     sql_keyword ------
       sql_ident        ---
        sql_func        ---------
       sql_ident            ---
        sql_func            ----
SELECT: Query the database and return zero or more rows of data.
EOF

run_test ./drive_sql_anno "SELECT       lower(abc)" 9

check_output "caret in keyword whitespace" <<EOF
                 SELECT       lower(abc)
     sql_keyword ------
       sql_ident              -----
        sql_func              ---------
       sql_ident                    ---
SELECT: Query the database and return zero or more rows of data.
EOF

run_test ./drive_sql_anno "SELECT lower(   abc    )" 14

check_output "caret in function whitespace" <<EOF
                 SELECT lower(   abc    )
     sql_keyword ------
       sql_ident        -----
        sql_func        ----------------
       sql_ident                 ---
lower: Returns a copy of the given string with all ASCII characters converted to lower case.
EOF

run_test ./drive_sql_anno "SELECT lower(abc" 16

check_output "caret in unfinished function call" <<EOF
                 SELECT lower(abc
     sql_keyword ------
       sql_ident        -----
        sql_func        ---------
       sql_ident              ---
lower: Returns a copy of the given string with all ASCII characters converted to lower case.
EOF

run_test ./drive_sql_anno "SELECT instr(lower(abc), '123')" 9

check_output "caret on the outer function" <<EOF
                 SELECT instr(lower(abc), '123')
     sql_keyword ------
       sql_ident        -----
        sql_func        -----------------------
       sql_ident              -----
        sql_func              ---------
       sql_ident                    ---
       sql_comma                        -
      sql_string                          -----
instr: Finds the first occurrence of the needle within the haystack and returns the number of prior characters plus 1, or 0 if the needle was not found
EOF

run_test ./drive_sql_anno "SELECT instr(lower(abc), '123')" 15

check_output "caret on a nested function" <<EOF
                 SELECT instr(lower(abc), '123')
     sql_keyword ------
       sql_ident        -----
        sql_func        -----------------------
       sql_ident              -----
        sql_func              ---------
       sql_ident                    ---
       sql_comma                        -
      sql_string                          -----
lower: Returns a copy of the given string with all ASCII characters converted to lower case.
EOF

run_test ./drive_sql_anno "SELECT instr(lower(abc), '123') FROM bar" 30

check_output "caret on a flag" <<EOF
                 SELECT instr(lower(abc), '123') FROM bar
     sql_keyword ------
       sql_ident        -----
        sql_func        -----------------------
       sql_ident              -----
        sql_func              ---------
       sql_ident                    ---
       sql_comma                        -
      sql_string                          -----
     sql_keyword                                 ----
       sql_ident                                      ---
SELECT: Query the database and return zero or more rows of data.
EOF

run_test ./drive_sql_anno "CREATE" 2

check_output "multiple help hits" <<EOF
                 CREATE
     sql_keyword ------
CREATE: Assign a name to a SELECT statement
CREATE: Create a table
EOF

run_test ./drive_sql_anno "SELECT 'hello, world!' FROM \"my table\""

check_output "string vs ident" <<EOF
                 SELECT 'hello, world!' FROM "my table"
     sql_keyword ------
      sql_string        ---------------
     sql_keyword                        ----
       sql_ident                             ----------
EOF

run_test ./drive_sql_anno "SELECT (1 + 2) AS three"

check_output "math" <<EOF
                 SELECT (1 + 2) AS three
     sql_keyword ------
     sql_garbage         -
        sql_oper           -
     sql_garbage             -
     sql_keyword                --
       sql_ident                   -----
EOF

run_test ./drive_sql_anno "SELECT * FROM (SELECT foo, bar FROM baz)"

check_output "subqueries" <<EOF
                 SELECT * FROM (SELECT foo, bar FROM baz)
     sql_keyword ------
        sql_oper        -
     sql_keyword          ----
     sql_keyword                ------
       sql_ident                       ---
       sql_comma                          -
       sql_ident                            ---
     sql_keyword                                ----
       sql_ident                                     ---
EOF
