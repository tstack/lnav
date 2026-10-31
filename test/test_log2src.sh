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

# The schema column is left out since it hashes the source file's full path,
# which differs from one checkout to another.
run_cap_test ${lnav_test} -nN \
    -c ":add-source-path ${test_dir}/log2src/python" \
    -c ";SELECT begin_line, end_line, language, function_name, qualified_name, block_id, level, pattern, path FROM source_log_stmt('log_example.py')" \
    -c ":write-jsonlines-to -"

run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT log_line, log_msg_src, log_msg_format, log_msg_values FROM all_logs" \
    -c ":write-jsonlines-to -" \
    ${test_dir}/log2src/java/java-example.0

run_cap_test ${lnav_test} -nN \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT begin_line, end_line, language, function_name, qualified_name, block_id, level, pattern, path FROM source_log_stmt('Server.java')" \
    -c ":write-jsonlines-to -"

# log_stmt_stats counts the same messages per statement as grouping
# all_logs does, and follows the LOG view's filters.
STMT_STATS_CHECK=";SELECT count(*) AS stmts, sum(s.hits = a.n) AS same_hits FROM log_stmt_stats s JOIN (SELECT log_msg_schema AS schema, count(*) AS n FROM all_logs WHERE log_msg_src IS NOT NULL GROUP BY 1) a ON s.schema = a.schema"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c "${STMT_STATS_CHECK}" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ":filter-out is slow" \
    -c "${STMT_STATS_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

# source_log_stmt() gives each statement's log_msg_schema, so a statement's
# messages can be found through it.
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) AS stmts, sum((SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema) = (SELECT count(*) FROM all_logs WHERE jget(log_msg_src, '/begin_line') = s.begin_line AND jget(log_msg_src, '/file') = s.path)) AS same_count, sum((SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema) > 0) AS logged FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java') s" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

# Once the statement map is running (querying log_stmt_stats starts it),
# all_logs finds a statement's messages through it for log_msg_schema
# constraints.  The rows are the same as when SQLite checks every message
# (the unary + keeps the constraint from being pushed down), in either
# direction and after a filter.
SCHEMA_ROWS_CHECK=";SELECT count(*) AS stmts, sum((SELECT group_concat(log_line) FROM all_logs WHERE log_msg_schema = s.schema) IS (SELECT group_concat(log_line) FROM all_logs WHERE +log_msg_schema = s.schema)) AS same_rows, sum((SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE log_msg_schema = s.schema ORDER BY log_line DESC)) IS (SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE +log_msg_schema = s.schema ORDER BY log_line DESC))) AS same_desc, sum((SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema) > 0) AS logged FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java') s"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${SCHEMA_ROWS_CHECK}" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ":filter-out is slow" \
    -c "${SCHEMA_ROWS_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

# A log_msg_schema IN (...) list is taken whole when the statement map can
# answer: each statement's rows are looked up and merged in order, so ORDER BY
# log_line ... LIMIT stops early instead of SQLite reading every row of each
# statement.  The rows match SQLite's own check (the unary +), in either
# direction, with a LIMIT, and after a filter.
SCHEMA_IN_CHECK=";SELECT (SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE log_msg_schema IN (SELECT schema FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java')) ORDER BY log_line)) IS (SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE +log_msg_schema IN (SELECT schema FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java')) ORDER BY log_line)) AS same_rows, (SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE log_msg_schema IN (SELECT schema FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java')) ORDER BY log_line DESC LIMIT 3)) IS (SELECT group_concat(r) FROM (SELECT log_line AS r FROM all_logs WHERE +log_msg_schema IN (SELECT schema FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java')) ORDER BY log_line DESC LIMIT 3)) AS same_last3, (SELECT count(*) FROM all_logs WHERE log_msg_schema IN (SELECT schema FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java'))) AS msgs"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${SCHEMA_IN_CHECK}" \
    -c ":write-csv-to -" \
    -c ":switch-to-view log" \
    -c ":filter-out is slow" \
    -c "${SCHEMA_IN_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

# With the statement map running, all_logs takes log_msg_src and
# log_msg_schema from it when nothing else needs log2src (no pattern, values,
# or stack trace), and knows which messages have no statement.  Both give
# the same answers as matching every message (log_msg_values forces that).
SRC_MAP_CHECK=";SELECT count(*) AS msgs, sum(m.src IS f.src) AS same_src, sum(m.schema IS f.schema) AS same_schema, sum(f.src IS NULL) AS unmatched FROM (SELECT log_line, log_msg_src AS src, log_msg_schema AS schema FROM all_logs) m JOIN (SELECT log_line, log_msg_src AS src, log_msg_schema AS schema, log_msg_values AS v FROM all_logs) f USING (log_line)"
cp ${test_dir}/log2src/java/java-example.0 log2src-unmatched.0
chmod ug+w log2src-unmatched.0
printf '2025-09-26 21:12:40,001 [main] INFO com.example.net.Server - no statement logs this\n' \
    >> log2src-unmatched.0
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${SRC_MAP_CHECK}" \
    -c ":write-csv-to -" \
    log2src-unmatched.0

# log_msg_schema_kind says where log_msg_schema comes from: 'statement' for the
# messages the statement map knows, 'message' (the text) for the unmatched one,
# since this format has no source fields.  The statement map answers both
# without reading the message; they're the same as when it's read
# (log_format_values reads it first).
SCHEMA_KIND_CHECK=";SELECT c.k AS kind, count(*) AS n, sum(c.k IS f.k) AS same_kind, sum((c.k = 'statement') = (f.src IS NOT NULL)) AS statement_iff_src FROM (SELECT log_line, log_msg_schema_kind AS k FROM all_logs) c JOIN (SELECT log_line, length(log_format_values) AS x, log_msg_schema_kind AS k, log_msg_src AS src FROM all_logs) f USING (log_line) GROUP BY 1 ORDER BY 1"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${SCHEMA_KIND_CHECK}" \
    -c ":write-csv-to -" \
    log2src-unmatched.0

# 'src_location' for unmatched messages of a format with source fields, whose
# kind depends on whether the message has both, so it's read even though the
# statement map knows the messages are unmatched.
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${SCHEMA_KIND_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_tabular.csv

# A time range with no rows leaves nothing for the statement map's rows to
# be taken from, in either direction.
EMPTY_TIME_CHECK=";SELECT (SELECT count(*) FROM all_logs WHERE log_msg_schema = s.schema AND log_time > '2026-01-01') AS asc_rows, (SELECT count(*) FROM (SELECT log_line FROM all_logs WHERE log_msg_schema = s.schema AND log_time > '2026-01-01' ORDER BY log_line DESC)) AS desc_rows FROM source_log_stmt('${test_dir}/log2src/java/com/example/net/Server.java') s LIMIT 1"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(*) FROM log_stmt_stats" \
    -c "${EMPTY_TIME_CHECK}" \
    -c ":write-csv-to -" \
    ${test_dir}/log2src/java/java-example.0

# A partial last line that is finished later is matched again.
head -3 ${test_dir}/log2src/java/java-example.0 > log2src-partial.0
printf '2025-09-26 21:12:39,386 [main] ERROR com.example.net.Server - inner' \
    >> log2src-partial.0
printf ' worker failed\n' > log2src-partial.rest
PARTIAL_CHECK=";SELECT (SELECT sum(hits) FROM log_stmt_stats) AS stats_hits, (SELECT count(*) FROM all_logs WHERE log_msg_src IS NOT NULL) AS matched"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c "${PARTIAL_CHECK}" \
    -c ":write-csv-to -" \
    -c ":sh cat log2src-partial.rest >> log2src-partial.0" \
    -c ":rebuild" \
    -c "${PARTIAL_CHECK}" \
    -c ":write-csv-to -" \
    log2src-partial.0

# A multi-line last message that gains a line later is matched again, so the
# statement map gives the statement that matches the whole message.
printf '2025-09-26 21:12:39,384 [main] INFO  com.example.Job - status ok\ndetails: all good\n' \
    > log2src-grow.0
printf 'trace: abc\n' > log2src-grow.rest
GROW_CHECK=";SELECT (SELECT group_concat(jget(src, '/begin_line')) FROM log_stmt_stats) AS stats_line, (SELECT jget(log_msg_src, '/begin_line') FROM all_logs) AS full_line, (SELECT log_msg_schema FROM all_logs) = (SELECT log_msg_schema || substr(log_msg_src, 1, 0) FROM all_logs) AS same_schema"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java-multiline" \
    -c "${GROW_CHECK}" \
    -c ":write-csv-to -" \
    -c ":sh cat log2src-grow.rest >> log2src-grow.0" \
    -c ":rebuild" \
    -c "${GROW_CHECK}" \
    -c ":write-csv-to -" \
    log2src-grow.0

# In headless mode, log_stmt_stats waits for the source scan to finish, even
# when nothing else waits between commands, as in a script file.  Without the
# wait, the query only sometimes runs before the scan is done, so this check
# can miss a regression.
cat > log2src-scan-wait.lnav <<EOF
:add-source-path ${test_dir}/log2src/java
;SELECT count(*) AS stmts, sum(hits) AS hits FROM log_stmt_stats
:write-csv-to -
EOF
run_cap_test ${lnav_test} -n \
    -f log2src-scan-wait.lnav \
    ${test_dir}/log2src/java/java-example.0

# Matching messages in all_logs doesn't hide them from a logline table.
for i in 1 2 3 4 5; do
    echo "2025-09-26 21:12:39,38$i [main] DEBUG com.example.net.Server - handling request from 10.0.0.$i"
done > log2src-logline.0
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT count(log_msg_schema) FROM all_logs" \
    -c ":goto 0" \
    -c ":create-logline-table matched" \
    -c ";SELECT count(*) AS n FROM matched" \
    -c ":write-csv-to -" \
    log2src-logline.0

# var_schema describes a statement's argument values, from sampled messages.
cat > log2src-meter.0 <<'METER'
2025-09-26 21:12:39,384 [main] INFO  com.example.net.Meter - batch of 12 took 4.5 ms for orders
2025-09-26 21:12:39,390 [main] INFO  com.example.net.Meter - batch of 3 took null ms for users
2025-09-26 21:12:39,401 [main] INFO  com.example.net.Meter - batch of 40 took 812.25 ms for orders
METER
VAR_SCHEMA_CHECK=";SELECT jget(var_schema, '/prefixItems/0/title') AS t0, jget(var_schema, '/prefixItems/0/type') AS type0, jget(var_schema, '/prefixItems/1/type') AS type1, jget(var_schema, '/prefixItems/2/type') AS type2, jget(var_schema, '/items') AS items FROM log_stmt_stats"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c "${VAR_SCHEMA_CHECK}" \
    -c ":write-csv-to -" \
    log2src-meter.0

# Arguments that are numbers with units are strings with a "measure" format.
cat > log2src-meter.1 <<'METER'
2025-09-26 21:12:39,384 [main] INFO  com.example.net.Meter - sent 1.5 KiB in 4.5ms
2025-09-26 21:12:39,390 [main] INFO  com.example.net.Meter - sent 512B in 2 s
2025-09-26 21:12:39,401 [main] INFO  com.example.net.Meter - sent 3 MB in 812ms
METER
MEASURE_CHECK=";SELECT jget(var_schema, '/prefixItems/0/title') AS t0, jget(var_schema, '/prefixItems/0/type') AS type0, jget(var_schema, '/prefixItems/0/format') AS format0, jget(var_schema, '/prefixItems/1/title') AS t1, jget(var_schema, '/prefixItems/1/type') AS type1, jget(var_schema, '/prefixItems/1/format') AS format1 FROM log_stmt_stats"
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c "${MEASURE_CHECK}" \
    -c ":write-csv-to -" \
    log2src-meter.1

# The source locations in stack frames are underlined.  Frames without a
# file, like "Unknown Source" and "Native Method", and text that only looks
# like a location are not.
run_cap_test ${lnav_test} -n \
    -c ";SELECT line, content FROM lnav_view_lines('log', 0, 40, 'html') WHERE content LIKE '%underline%'" \
    -c ":write-raw-to -" \
    ${test_dir}/logfile_stack_frames.0

# In the HTML, each frame is an anchor with its location as JSON
# (data-lnav-frame), for clients that open the source themselves, and a
# file:// link when its path is a file here.
printf '2024-03-01T10:00:03.000Z ERROR go server panicked\n\t%s:7 +0x1d\n\t/nope/main.go:9 +0x2a\n' \
    "${test_dir}/log2src/go/cmd/server/main.go" > stack-frame-local.log
run_cap_test ${lnav_test} -n \
    -c ";SELECT line, content FROM lnav_view_lines('log', 0, 5, 'html') WHERE content LIKE '%stack-frame%'" \
    -c ":write-raw-to -" \
    stack-frame-local.log

# In a JSON log, a trace in a field is shown on detail lines, like
# "  exc: <frame>", and the patterns are matched against the value.
run_cap_test ${lnav_test} -n \
    -c ";SELECT line, content FROM lnav_view_lines('log', 0, 10, 'html') WHERE content LIKE '%underline%'" \
    -c ":write-raw-to -" \
    ${test_dir}/logfile_stack_frames.json

# source_find_file() takes an existing absolute path as-is and looks for
# others in the source paths, by their full path and then by their suffixes,
# for any language.
run_cap_test ${lnav_test} -nN \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ":add-source-path ${test_dir}/log2src/go" \
    -c ";SELECT source_find_file('${test_dir}/log2src/python/log_example.py') AS abs_path, source_find_file('com/example/net/Server.java') AS rel_path, source_find_file('/build/agent/work/com/example/net/Server.java') AS foreign_path, source_find_file('./cmd/server/main.go') AS go_path, source_find_file('nope/Missing.java') AS missing_path" \
    -c ":write-json-to -"

run_cap_test ${lnav_test} -nN \
    -c "|lnav-stack-frame-handler right 'com/example/net/Server.java' 10"

run_cap_test ${lnav_test} -nN \
    -c "|lnav-stack-frame-handler left 'nope/Missing.java' 10"

# A stack frame pattern needs the 'file' and 'line' captures.
run_cap_test ${lnav_test} -W -n \
    -I ${test_dir}/bad-config-stack-frame \
    ${test_dir}/logfile_stack_frames.0

# log_stack_frames counts, for each location in a stack trace, the messages
# whose trace has it.  With no source paths, every pattern is used; frames
# without a line, like "Unknown Source" and "Native Method", aren't counted.
run_cap_test ${lnav_test} -n \
    -c ";SELECT path, line, hits, src_path FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_stack_frames.0

# In a JSON log, the frames come from the decoded values.
run_cap_test ${lnav_test} -n \
    -c ";SELECT path, line, hits FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_stack_frames.json

# A message is counted once for a frame, however often the frame is in its
# trace.
printf '2024-03-01T10:00:00.000Z ERROR deep\njava.lang.StackOverflowError\n\tat com.foo.Rec.down(Rec.java:5)\n\tat com.foo.Rec.down(Rec.java:5)\n\tat com.foo.Rec.down(Rec.java:5)\n2024-03-01T10:00:01.000Z ERROR again\njava.lang.IllegalStateException: once\n\tat com.foo.Rec.down(Rec.java:5)\n\tat com.foo.Rec.start(Rec.java:9)\n' > stack-frame-repeat.log
run_cap_test ${lnav_test} -n \
    -c ";SELECT path, line, hits FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    stack-frame-repeat.log

# The counts are for the messages the LOG view shows, after filters.
run_cap_test ${lnav_test} -n \
    -c ":filter-out again" \
    -c ";SELECT path, line, hits FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    stack-frame-repeat.log

# src_path is the file under the source paths that a frame's path is, found
# by its trailing directories, or NULL.
printf '2024-03-01T10:00:00.000Z ERROR server failed\njava.lang.IllegalStateException: boom\n\tat com.example.net.Server.handle(Server.java:14)\n\tat com.example.other.Missing.run(Missing.java:3)\n' > stack-frame-src.log
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT path, line, hits, src_path LIKE '%/log2src/java/com/example/net/Server.java' AS in_source FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    stack-frame-src.log

# Once log2src has found log statements, only the patterns for their
# languages are used: with Java source, only the Java frames are counted.
run_cap_test ${lnav_test} -n \
    -c ":add-source-path ${test_dir}/log2src/java" \
    -c ";SELECT path, line, hits FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_stack_frames.0

# A stack frame pattern's languages have to be known ones.
run_cap_test ${lnav_test} -W -n \
    -I ${test_dir}/bad-config-stack-frame-lang \
    ${test_dir}/logfile_stack_frames.0

# A JSON message with bytes that aren't UTF-8, and one with a lone low
# surrogate escape, which is decoded as "?": their frames are still found.
printf '{"name":"w","hostname":"h","pid":1,"level":50,"msg":"bad \xff byte","time":"2024-03-01T10:00:00.000Z","v":0,"exc":"java.lang.X: x\\n\\tat com.foo.A.b(A.java:1)"}\n{"name":"w","hostname":"h","pid":1,"level":50,"msg":"lone \\udc80 here","time":"2024-03-01T10:00:01.000Z","v":0,"exc":"java.lang.Y: y\\n\\tat com.foo.A.c(A.java:2)"}\n' > stack-frame-utf.json
run_cap_test ${lnav_test} -n \
    -c ";SELECT path, line, hits FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    stack-frame-utf.json

run_cap_test ${lnav_test} -n \
    stack-frame-utf.json

# When more of a message is read, its frames are found again, without
# counting the earlier ones twice.
printf '2024-03-01T10:00:00.000Z ERROR boom\njava.lang.X: x\n\tat com.foo.G.a(G.java:1)\n' > stack-frame-grow.log
printf '\tat com.foo.G.b(G.java:2)\n2024-03-01T10:00:01.000Z ERROR boom2\njava.lang.Y: y\n\tat com.foo.G.a(G.java:1)\n' > stack-frame-grow.more
run_cap_test ${lnav_test} -n \
    -c ";SELECT count(*) FROM log_stack_frames" \
    -c ":sh cat stack-frame-grow.more >> stack-frame-grow.log" \
    -c ":rebuild" \
    -c ";SELECT path, line, hits FROM log_stack_frames ORDER BY path, line" \
    -c ":write-csv-to -" \
    stack-frame-grow.log

# log_stack_frame_rows() lists the rows of the messages with a frame in their
# stack trace, by its log_stack_frames rowid, in order.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line FROM log_stack_frame_rows((SELECT rowid FROM log_stack_frames WHERE path = 'com/foo/Rec.java' AND line = 5))" \
    -c ":write-csv-to -" \
    stack-frame-repeat.log

# A frame that isn't in the table, a NULL frame, or no frame at all gives no
# rows; ORDER BY log_line DESC is SQLite's to sort.
run_cap_test ${lnav_test} -n \
    -c ";SELECT (SELECT count(*) FROM log_stack_frame_rows(999999)) AS unknown, (SELECT count(*) FROM log_stack_frame_rows(NULL)) AS null_frame, (SELECT count(*) FROM log_stack_frame_rows) AS no_frame" \
    -c ":write-csv-to -" \
    -c ";SELECT log_line FROM log_stack_frame_rows((SELECT rowid FROM log_stack_frames WHERE path = 'com/foo/Rec.java' AND line = 5)) ORDER BY log_line DESC" \
    -c ":write-csv-to -" \
    stack-frame-repeat.log

# The rows are the LOG view's, so a message a filter hides isn't listed.
run_cap_test ${lnav_test} -n \
    -c ":filter-out again" \
    -c ";SELECT log_line FROM log_stack_frame_rows((SELECT rowid FROM log_stack_frames WHERE path = 'com/foo/Rec.java' AND line = 5))" \
    -c ":write-csv-to -" \
    stack-frame-repeat.log

# When more of a message is read, its rows are listed again for the frames it
# has now, and a message after it is listed too.
printf '2024-03-01T10:00:00.000Z ERROR boom\njava.lang.X: x\n\tat com.foo.H.a(H.java:1)\n' > stack-frame-grow-rows.log
printf '\tat com.foo.H.b(H.java:2)\n2024-03-01T10:00:01.000Z ERROR boom2\njava.lang.Y: y\n\tat com.foo.H.a(H.java:1)\n' > stack-frame-grow-rows.more
run_cap_test ${lnav_test} -n \
    -c ";SELECT 'before' AS w, log_line FROM log_stack_frame_rows((SELECT rowid FROM log_stack_frames WHERE path = 'com/foo/H.java' AND line = 1))" \
    -c ":write-csv-to -" \
    -c ":sh cat stack-frame-grow-rows.more >> stack-frame-grow-rows.log" \
    -c ":rebuild" \
    -c ";SELECT f.line AS frame_line, r.log_line FROM log_stack_frames f, log_stack_frame_rows(f.rowid) r WHERE f.path = 'com/foo/H.java' ORDER BY f.line, r.log_line" \
    -c ":write-csv-to -" \
    stack-frame-grow-rows.log

# The same for a log_msg_schema lookup: the grown message has another schema,
# and it's found by that one, not the one it had before.
printf '2024-03-01T10:00:00.000Z ERROR boom\njava.lang.X: x\n\tat com.foo.H.a(H.java:1)\n' > stack-frame-grow-schema.log
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line FROM all_logs WHERE log_msg_schema = (SELECT log_msg_schema FROM all_logs WHERE log_line = 0)" \
    -c ":sh cat stack-frame-grow-rows.more >> stack-frame-grow-schema.log" \
    -c ":rebuild" \
    -c ";SELECT log_line FROM all_logs WHERE log_msg_schema = (SELECT log_msg_schema FROM all_logs WHERE log_line = 0)" \
    -c ":write-csv-to -" \
    stack-frame-grow-schema.log

# all_logs takes a log_line IN list whole: its rows in order, NULLs and rows
# that aren't a message's start left out, clipped by other log_line bounds.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line FROM all_logs WHERE log_line IN (5, 0, 1, NULL, 5) ORDER BY log_line DESC" \
    -c ":write-csv-to -" \
    -c ";SELECT log_line FROM all_logs WHERE log_line IN (0, 5) AND log_line > 0" \
    -c ":write-csv-to -" \
    stack-frame-repeat.log

# A frame's messages through all_logs; with another term in an OR, the rows
# are the frame's and the other term's.
run_cap_test ${lnav_test} -n \
    -c ";SELECT log_line FROM all_logs WHERE log_line IN (SELECT log_line FROM log_stack_frame_rows((SELECT rowid FROM log_stack_frames WHERE path = 'com/foo/Rec.java' AND line = 9))) OR log_body LIKE '%deep%' ORDER BY log_line" \
    -c ":write-csv-to -" \
    stack-frame-repeat.log
