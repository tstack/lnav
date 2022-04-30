#! /bin/bash

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc[', '<abc/>')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc', '<abc')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc/def', '<abc/>')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc/def[@a=\"b\"]', '<abc><def/><def a=\"b\">ghi</def></abc>')"

run_cap_test ./drive_sql "SELECT * FROM xpath('/abc/def', '<abc><def>Hello &gt;</def></abc>')"
