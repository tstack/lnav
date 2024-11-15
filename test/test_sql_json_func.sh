#! /bin/bash

run_cap_test ./drive_sql "select json_concat('[null,', 1.0, 2.0)"

run_cap_test ./drive_sql "select json_concat(json('[null, true, 0]'), 1.0, 2.0)"

run_cap_test ./drive_sql "select json_concat(json('[\"tag0\"]'), 'tag1', 'tag2')"

run_cap_test ./drive_sql "select json_concat(NULL, NULL)"

run_cap_test ./drive_sql "select json_concat(NULL, json('{\"abc\": 1}'))"

run_cap_test ./drive_sql "select json_contains(NULL, 4)"

run_cap_test ./drive_sql "select json_contains('', 4)"

run_cap_test ./drive_sql "select json_contains('null', NULL)"

run_cap_test ./drive_sql "select json_contains('[[0]]', 0)"

run_cap_test ./drive_sql "select json_contains('4', 4)"

run_cap_test ./drive_sql "select json_contains('4', 2)"

run_cap_test env TEST_COMMENT='contains1' ./drive_sql <<EOF
select json_contains('"hi"', 'hi')
EOF

run_cap_test env TEST_COMMENT='contains1.5' ./drive_sql <<EOF
select json_contains('"hi"', 'hi there')
EOF

run_cap_test env TEST_COMMENT='contains2' ./drive_sql <<EOF
select json_contains('["hi", "bye"]', 'hola') as res
EOF

run_cap_test env TEST_COMMENT='contains3' ./drive_sql <<EOF
select json_contains('["hi", "bye", "solong"]', 'bye') as res
EOF

run_cap_test env TEST_COMMENT='contains4' ./drive_sql <<EOF
select json_contains('["hi", "bye", "solong]', 'bye') as res
EOF

run_cap_test ./drive_sql "select jget()"

run_cap_test ./drive_sql "select jget('#', '/0')"

run_cap_test ./drive_sql "select jget('[123, true', '/0')"

run_cap_test ./drive_sql "select jget('4', '')"

run_cap_test ./drive_sql "select jget('4', null)"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/3')"

run_cap_test ./drive_sql "select typeof(jget('[null, true, 20, 30, 40]', '/3'))"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40, {\"msg\": \"Hello\"}]', '/5')"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40, {\"msg\": \"Hello\"}]', '/5/msg')"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40, {\"msg\": \"Hello\"}]', '')"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/abc')"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/abc', 1)"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/0')"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40]', '/0/foo')"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 4.0]', '/4')"

run_cap_test ./drive_sql "select typeof(jget('[null, true, 20, 30, 4.0]', '/4'))"

run_cap_test ./drive_sql "select jget('[null, true, 20, 30, 40', '/0/foo')"

run_cap_test ./drive_sql "select json_group_object(key) from (select 1 as key)"

GROUP_SELECT_1=$(cat <<EOF
SELECT id, json_group_object(key, value) as stack FROM (
              SELECT 1 as id, 'key1' as key, 10 as value
    UNION ALL SELECT 1 as id, 'key2' as key, 20 as value
    UNION ALL SELECT 1 as id, 'key3' as key, 30 as value)
EOF
)

run_cap_test ./drive_sql "$GROUP_SELECT_1"

GROUP_SELECT_2=$(cat <<EOF
SELECT id, json_group_object(key, value) as stack FROM (
              SELECT 1 as id, 1 as key, 10 as value
    UNION ALL SELECT 1 as id, 2 as key, null as value
    UNION ALL SELECT 1 as id, 3 as key, 30.5 as value)
EOF
)

run_cap_test ./drive_sql "$GROUP_SELECT_2"

if test x"$HAVE_SQLITE3_VALUE_SUBTYPE" != x""; then
    GROUP_SELECT_3=$(cat <<EOF
SELECT id, json_group_object(key, json(value)) as stack FROM (
              SELECT 1 as id, 1 as key, 10 as value
    UNION ALL SELECT 1 as id, 2 as key, json_array(1, 2, 3) as value
    UNION ALL SELECT 1 as id, 3 as key, 30.5 as value)
EOF
)

    run_cap_test ./drive_sql "$GROUP_SELECT_3"
fi


GROUP_ARRAY_SELECT_1=$(cat <<EOF
SELECT json_group_array(value) as stack FROM (
              SELECT 10 as value
    UNION ALL SELECT null as value
    UNION ALL SELECT 'hello' as value)
EOF
)

run_cap_test ./drive_sql "$GROUP_ARRAY_SELECT_1"

GROUP_ARRAY_SELECT_2=$(cat <<EOF
SELECT json_group_array(value, value * 10) as stack FROM (
              SELECT 10 as value
    UNION ALL SELECT 20 as value
    UNION ALL SELECT 30 as value)
EOF
)

run_cap_test ./drive_sql "$GROUP_ARRAY_SELECT_2"

run_cap_test ./drive_sql "SELECT json_group_array(column1) FROM (VALUES (1)) WHERE 0"