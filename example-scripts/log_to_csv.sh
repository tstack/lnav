#!/usr/bin/env bash

#
# An example script that converts messages in a syslog file into a
# CSV-formatted file.  The CSV file is written to the current directory
# with the same base name as the source file.  If the script is run on
# the same file multiple times, it will only convert newly added lines.
#
# NOTE: lnav is going to save some state in $HOME; you might want to change
# $HOME to something else...
#
# NOTE 2: Unfortunately, this is pretty inefficient right now since lnav
# is going to store the entire log file in memory when processing the
# result of the SQL SELECT.
#

if test $# -lt 1; then
    echo "usage: $0 <path>"
    echo "Convert a syslog file into CSV format."
    exit 1
fi

if test ! -f "$1"; then
    echo "error: expecting a log file as the first argument"
    exit 1
fi

# Figure out a unique file name.
out_file_base=$(basename "$1")
counter=0
while test -e "${out_file_base}.${counter}.csv"; do
    counter=$((counter + 1))
done
export OUT_FILE="${out_file_base}.${counter}.csv"

# Here's a quick summary of what this is doing:
#
# 1. ':load-session' will load the session data which stores which lines
#    are bookmarked in a file.  We're using bookmarks to keep track of the
#    last line that we converted in a previous run of this script.
# 2. ';CREATE TABLE helper' creates a temporary table that we use to store
#    the range of messages that we'll be converting.
# 3. ';INSERT INTO helper' will figure out the range of lines in syslog file
#    to convert.
# 4. ';UPDATE syslog_log' will set a bookmark on the last line of the range
#    we computed in the previous step.
# 5. ';SELECT *' will pull all of the log messages in the computed range.
# 6. ':write-csv-to' will write out the log messages SELECTed in step #5.
# 7. ':save-session' will save the bookmark we set so it can be loaded on
#    future runs of this script.

lnav -nq -d /tmp/lnav.err \
    -c ":load-session" \
    -c ";CREATE TABLE helper ( start_line int, max_line int )" \
    -c ";INSERT INTO helper ( start_line, max_line ) VALUES (\
        (SELECT coalesce(\
            (SELECT max(log_line) FROM syslog_log where log_mark = 1) + 1,\
            0)),\
        (SELECT max(log_line) FROM syslog_log ))" \
    -c ";UPDATE syslog_log SET log_mark = 1 where log_line = (\
          SELECT max_line FROM helper)" \
    -c ";SELECT *,log_text FROM syslog_log where log_line between (\
        SELECT start_line FROM helper) and (SELECT max_line FROM helper)" \
    -c ':write-csv-to $OUT_FILE' \
    -c ":save-session" \
    "$1"
