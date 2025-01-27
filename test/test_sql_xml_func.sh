#! /bin/bash

export YES_COLOR=1

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc[', '<abc/>')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc', '<abc')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc/def', '<abc/>')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc/def[@a=\"b\"]', '<abc><def/><def a=\"b\">ghi</def></abc>')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc/def', '<abc><def>Hello &gt;</def></abc>')"

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM xpath('/catalog', (SELECT content FROM lnav_file LIMIT 1))" \
    ${test_dir}/invalid-books.xml

run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM xpath('/cat[alog', (SELECT content FROM lnav_file LIMIT 1))" \
    ${test_dir}/books.xml

run_cap_test env TEST_XP1="//book[1]/price" TEST_XP2="//book[2]/price" \
    ${lnav_test} -n \
    -c ";SELECT * FROM environ, xpath(environ.value, (SELECT content FROM lnav_file)) WHERE name LIKE 'TEST_XP%'" \
    ${test_dir}/books.xml
