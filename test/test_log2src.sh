#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -nN \
    -c ":add-source-path"

run_cap_test ${lnav_test} -nN \
    -c ":add-source-path non-existent"

run_cap_test ${lnav_test} -nN \
    -c ":add-source-path ~root"

run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/python" \
    -c ";SELECT log_line, log_msg_src, log_msg_format, log_msg_values FROM all_logs" \
    -c ":write-jsonlines-to -" \
    ${test_dir}/log2src/python/python-example.0

run_cap_test ${lnav_test} -nN \
    -c ":add-source-path ${test_dir}/log2src/python" \
    -c ";SELECT * FROM source_log_stmt('log_example.py')" \
    -c ":write-jsonlines-to -"

run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT log_line, log_msg_src, log_msg_format, log_msg_values FROM all_logs" \
    -c ":write-jsonlines-to -" \
    ${test_dir}/log2src/java/java-example.0

run_cap_test ${lnav_test} -nN \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT * FROM source_log_stmt('Server.java')" \
    -c ":write-jsonlines-to -"

# log_stmt_stats counts the same messages per statement as grouping
# all_logs does, and follows the LOG view's filters.
STMT_STATS_CHECK=";SELECT count(*) AS stmts, sum(s.hits = a.n) AS same_hits FROM log_stmt_stats s JOIN (SELECT log_msg_schema AS schema, count(*) AS n FROM all_logs WHERE log_msg_src IS NOT NULL GROUP BY 1) a ON s.schema = a.schema"
run_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c "${STMT_STATS_CHECK}" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ":filter-out is slow" \
    -c "${STMT_STATS_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

check_output "log_stmt_stats does not match all_logs?" <<EOF
stmts,same_hits
4,4
stmts,same_hits
3,3
EOF

# source_log_stmt() gives each statement's log_msg_schema, so a statement's
# messages can be found through it.
run_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) AS stmts, sum((SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema) = (SELECT count(*) FROM all_logs WHERE jget(log_msg_src, '/begin_line') = s.begin_line AND jget(log_msg_src, '/file') = s.path)) AS same_count, sum((SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema) > 0) AS logged FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java') s" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

check_output "source_log_stmt schema does not match all_logs?" <<EOF
stmts,same_count,logged
4,4,4
EOF

# Once the statement map is running (querying log_stmt_stats starts it),
# all_logs finds a statement's messages through it for log_msg_schema
# constraints.  The rows are the same as when SQLite checks every message
# (the unary + keeps the constraint from being pushed down), in either
# direction and after a filter.
SCHEMA_ROWS_CHECK=";SELECT count(*) AS stmts, sum((SELECT group_concat(log_line) FROM all_logs WHERE log_msg_schema = s.schema) IS (SELECT group_concat(log_line) FROM all_logs WHERE +log_msg_schema = s.schema)) AS same_rows, sum((SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE log_msg_schema = s.schema ORDER BY log_line DESC)) IS (SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE +log_msg_schema = s.schema ORDER BY log_line DESC))) AS same_desc, sum((SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema) > 0) AS logged FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java') s"
run_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${SCHEMA_ROWS_CHECK}" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ":filter-out is slow" \
    -c "${SCHEMA_ROWS_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

check_output "all_logs schema rows from the statement map are wrong?" <<EOF
stmts,same_rows,same_desc,logged
4,4,4,4
stmts,same_rows,same_desc,logged
4,4,4,3
EOF

# A time range with no rows leaves nothing for the statement map's rows to
# be taken from, in either direction.
EMPTY_TIME_CHECK=";SELECT (SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema AND log_time > '2026-01-01') AS asc_rows, (SELECT count(*) FROM (SELECT log_line FROM all_logs WHERE log_msg_schema = s.schema AND log_time > '2026-01-01' ORDER BY log_line DESC)) AS desc_rows FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java') s LIMIT 1"
run_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${EMPTY_TIME_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

check_output "all_logs schema rows with an empty time range are wrong?" <<EOF
asc_rows,desc_rows
0,0
EOF

# A partial last line that is finished later is matched again.
head -3 ${test_dir}/log2src/java/java-example.0 > log2src-partial.0
printf '2025-09-26 21:12:39,386 [main] ERROR com.example.net.Server - inner' \
    >> log2src-partial.0
printf ' worker failed\n' > log2src-partial.rest
PARTIAL_CHECK=";SELECT (SELECT sum(hits) FROM log_stmt_stats) AS stats_hits, (SELECT count(*) FROM all_logs WHERE log_msg_src IS NOT NULL) AS matched"
run_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c "${PARTIAL_CHECK}" \
    -c ":write-csv-to -" \
    -c ":sh cat log2src-partial.rest >> log2src-partial.0" \
    -c ":rebuild" \
    -c "${PARTIAL_CHECK}" \
    -c ":write-csv-to -" \
    log2src-partial.0

check_output "a finished partial line is not matched again?" <<EOF
stats_hits,matched
3,3
stats_hits,matched
4,4
EOF

# A multi-line last message that gains a line later is matched again, so the
# statement map gives the statement that matches the whole message.
printf '2025-09-26 21:12:39,384 [main] INFO  com.example.Job - status ok\ndetails: all good\n' \
    > log2src-grow.0
printf 'trace: abc\n' > log2src-grow.rest
GROW_CHECK=";SELECT (SELECT group_concat(jget(src, '/begin_line')) FROM log_stmt_stats) AS stats_line, (SELECT jget(log_msg_src, '/begin_line') FROM all_logs) AS full_line, (SELECT log_msg_schema FROM all_logs) = (SELECT log_msg_schema || substr(log_msg_src, 1, 0) FROM all_logs) AS same_schema"
run_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java-multiline" \
    -c "${GROW_CHECK}" \
    -c ":write-csv-to -" \
    -c ":sh cat log2src-grow.rest >> log2src-grow.0" \
    -c ":rebuild" \
    -c "${GROW_CHECK}" \
    -c ":write-csv-to -" \
    log2src-grow.0

check_output "a multi-line message that grew is not matched again?" <<EOF
stats_line,full_line,same_schema
11,11,1
stats_line,full_line,same_schema
12,12,1
EOF

# In headless mode, log_stmt_stats waits for the source scan to finish, even
# when nothing else waits between commands, as in a script file.  Without the
# wait, the query only sometimes runs before the scan is done, so this check
# can miss a regression.
cat > log2src-scan-wait.lnav <<EOF
:add-source-path ${test_dir}/log2src/java
;SELECT count(*) AS stmts, sum(hits) AS hits FROM log_stmt_stats
:write-csv-to -
EOF
run_test ${lnav_test} -n \
    -f log2src-scan-wait.lnav \
    ${test_dir}/log2src/java/java-example.0

check_output "log_stmt_stats did not wait for the source scan?" <<EOF
stmts,hits
4,4
EOF

# Matching messages in all_logs doesn't hide them from a logline table.
for i in 1 2 3 4 5; do
    echo "2025-09-26 21:12:39,38$i [main] DEBUG com.example.net.Server - handling request from 10.0.0.$i"
done > log2src-logline.0
run_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(log_msg_schema) FROM all_logs" \
    -c ":goto 0" \
    -c ":create-logline-table matched" \
    -c ";SELECT count(*) AS n FROM matched" \
    -c ":write-csv-to -" \
    log2src-logline.0

check_output "logline table misses messages all_logs matched?" <<EOF
n
5
EOF
