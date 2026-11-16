#! /bin/bash

export TZ=UTC
export YES_COLOR=1

# look up commands by tag, with how to call them and what undoes them
run_cap_test ${lnav_test} -n \
    -c ";SELECT name, synopsis, summary, opposites FROM lnav_help WHERE json_contains(tags, 'filtering') ORDER BY name" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

# a command's full entry, with examples that can be run as written
run_cap_test ${lnav_test} -n \
    -c ";SELECT * FROM lnav_help WHERE name = ':filter-out'" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_access_log.0

# aliases are listed on the command they name
run_cap_test ${lnav_test} -n \
    -c ";SELECT name, aliases FROM lnav_help WHERE aliases != '[]'" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

# SQL functions and keywords, and PRQL transforms (no ORDER BY: sorting
# drops the JSON subtype, so parameters would come out as a string)
run_cap_test ${lnav_test} -n \
    -c ";SELECT kind, name, synopsis, parameters, prql_path FROM lnav_help WHERE name IN ('regexp_match', 'SELECT', 'from')" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_access_log.0

# a table's entry carries its commented CREATE TABLE
run_cap_test ${lnav_test} -n \
    -c ";SELECT kind, summary, synopsis FROM lnav_help WHERE name = 'lnav_view_filters'" \
    -c ":write-json-to -" \
    ${test_dir}/logfile_access_log.0

# every entry, tables included, says what it is
run_cap_test ${lnav_test} -n \
    -c ";SELECT kind, name FROM lnav_help WHERE summary IS NULL OR summary = ''" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

# every command declares what running it can change
run_cap_test ${lnav_test} -n \
    -c ";SELECT name FROM lnav_help WHERE kind = 'command' AND effect = 'unknown'" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

# one command of each effect; other kinds of entries have none
run_cap_test ${lnav_test} -n \
    -c ";SELECT name, effect FROM lnav_help WHERE name IN (':current-time', ':write-json-to', ':filter-out', ':config', ':sh', ':quit', 'regexp_match', 'lnav_help')" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0
