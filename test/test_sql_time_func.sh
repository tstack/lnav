#! /bin/bash

export TZ=UTC
export YES_COLOR=1

# timeslice('blah')
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'blah')"

# before 12pm
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'before fri')"

# not before 12pm
run_cap_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'after fri')"

# not before 12pm
run_cap_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'fri')"

# before 12pm
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'before 12pm')"

# not before 12pm
run_cap_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'before 12pm')"

# after 12pm
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', 'after 12pm')"

# not after 12pm
run_cap_test ./drive_sql "select timeslice('2015-08-07 11:59:00', 'after 12pm')"

# timeslice()
run_cap_test ./drive_sql "select timeslice()"

# timeslice('2015-02-01T05:10:00')
run_cap_test ./drive_sql "select timeslice('2015-02-01T05:10:00')"

# timeslice empty
run_cap_test ./drive_sql "select timeslice('', '')"

# timeslice abs
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '8 am')"

# timeslice abs
run_cap_test ./drive_sql "select timeslice('2015-08-07 08:00:33', '8 am')"

# timeslice abs
run_cap_test ./drive_sql "select timeslice('2015-08-07 08:01:33', '8 am')"

# timeslice(null, null)
run_cap_test ./drive_sql "select timeslice(null, null)"

# timeslice(null)
run_cap_test ./drive_sql "select timeslice(null)"

# 100ms slice
run_cap_test ./drive_sql "select timeslice(1616300753.333, '100ms')"

# timeslice 5m
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '5m')"

# timeslice 1d
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '1d')"

# XXX This is wrong...
# timeslice 1 month
run_cap_test ./drive_sql "select timeslice('2015-08-07 12:01:00', '1 month')"

# timeslice ms
run_cap_test ./drive_sql "select timediff('2017-01-02T05:00:00.100', '2017-01-02T05:00:00.000')"

# timeslice day
run_cap_test ./drive_sql "select timediff('today', 'yesterday')"

# timeslice day
run_cap_test ./drive_sql "select timediff('foo', 'yesterday')"

run_cap_test ./drive_sql "SELECT timezone('America/Los_Angeles', '2022-03-02T10:00')"

run_cap_test ./drive_sql "SELECT timezone('America/Los_Angeles', '2022-03-02T10:20:30.400-0700')"

run_cap_test ./drive_sql "SELECT timezone('America/Los_Angeles', '2022-04-02T10:20:30.400-0700')"

run_cap_test ./drive_sql "SELECT timezone('America/New_York', '2022-03-02T10:20:30.400-0700')"

run_cap_test ./drive_sql "SELECT timezone('UTC', '2022-03-02T10:20:30.400-0700')"

run_cap_test ${lnav_test} -nN -c ";SELECT timezone('bad-zone', '2022-03-02T10:20:30.400-0700')"

run_cap_test ${lnav_test} -nN -c ";SELECT timezone('UTC', '2022-03-02T10:20:30+')"

# a timestamp in a different format from the one before it in the session
run_cap_test ${lnav_test} -nN \
    -c ";SELECT timezone('UTC', '2022-03-02T10:00') AS a" \
    -c ";SELECT timezone('UTC', '2022-03-02 10:00:00.123') AS b"

# a format that only matches the start of the next timestamp is not used
run_cap_test ${lnav_test} -nN \
    -c ";SELECT timezone('UTC', '2022-03-02T10:00') AS a" \
    -c ";SELECT timezone('UTC', '2022-03-02T10:00:30') AS b, timeslice('2022-03-02T10:00:30', '1s') AS c"

# the same wall-clock time with different offsets
run_cap_test ${lnav_test} -nN \
    -c ";SELECT timezone('UTC', '2022-03-02T10:20:30-0700') AS a, timezone('UTC', '2022-03-02T10:20:30+0000') AS b"

run_cap_test ${lnav_test} -nN -c ";SELECT humanize_duration(1.7e12)"

run_cap_test ${lnav_test} -nN -c ";SELECT humanize_duration(1e400)"

run_cap_test ${lnav_test} -nN -c ";SELECT timeslice(-1500, '1s')"

run_cap_test ${lnav_test} -nN -c ";SELECT timeslice(1e300, '1s')"

run_cap_test ./drive_sql "SELECT timeslice(1500, '1s')"
