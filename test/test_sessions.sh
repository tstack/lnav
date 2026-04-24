#! /bin/bash

export TZ=UTC
export HOME="./sessions"
unset XDG_CONFIG_HOME
rm -rf "./sessions"
mkdir -p $HOME

cat ${test_dir}/logfile_access_log.0 | run_cap_test ${lnav_test} -n \
    -c ":filter-out vmk" \
    -c ":export-session-to exported-stdin-session.0.lnav"

# run_cap_test cat exported-stdin-session.0.lnav

run_cap_test ${lnav_test} -nN \
    -c "|exported-stdin-session.0.lnav"

run_cap_test ${lnav_test} -n \
    -e "cat ${test_dir}/logfile_access_log.0" \
    -c ":filter-out vmk" \
    -c ":export-session-to exported-sh-session.0.lnav"

run_cap_test ${lnav_test} -nN \
    -c "|exported-sh-session.0.lnav"

run_cap_test ${lnav_test} -n \
    -c ":reset-session" \
    -c ":goto 0" \
    -c ":hide-file" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.*

# hidden file saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    ${test_dir}/logfile_access_log.*

# setting log_mark
run_cap_test ${lnav_test} -nq \
    -c ":reset-session" \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ":goto 1" \
    -c ":partition-name middle" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

mkdir -p support-dump
echo 'Hello' > support-dump/readme
cp ${test_dir}/logfile_access_log.0 support-dump/
cp ${test_dir}/logfile_access_log.1 support-dump/

run_cap_test ${lnav_test} -nq \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ":goto 1" \
    -c ":hide-file */logfile_access_log.1" \
    -c ":export-session-to -" \
    support-dump/logfile_access_log.*

run_cap_test ${lnav_test} -nq \
    -c ";update access_log set log_mark = 1 where sc_bytes > 60000" \
    -c ":hide-fields cs_user_agent" \
    -c ":highlight vmw" \
    -c ":set-min-log-level debug" \
    -c ":hide-lines-before 2005" \
    -c ":hide-lines-after 2030" \
    -c ":filter-out blah" \
    -c "/foobar" \
    -c ":goto 1" \
    -c ":export-session-to exported-session.0.lnav" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c "|exported-session.0.lnav" \
    -c ";SELECT * FROM lnav_view_filters" \
    -c ":write-screen-to -" \
    -c ";SELECT name,search FROM lnav_views" \
    -c ":write-screen-to -" \
    ${test_dir}/logfile_access_log.0

# log mark was not saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ':write-to -' \
    ${test_dir}/logfile_access_log.0

# file was not closed
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":close" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# partition name was not saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ';select log_line,log_part from access_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_access_log.0

# partition name was not saved in session and doesn't not show up in crumbs
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ':goto 2' \
    -c ";SELECT top_meta FROM lnav_views WHERE name = 'log'" \
    -c ':write-json-to -' \
    ${test_dir}/logfile_access_log.0

# adjust time is not working
run_cap_test ${lnav_test} -nq \
    -c ":adjust-log-time 2010-01-01T00:00:00" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# adjust time is not saved in session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":test-comment adjust time in session" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":clear-adjusted-log-time" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":reset-session" \
    -c ":save-session" \
    -c ":test-comment reset adjusted time" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":test-comment reset of adjust stuck" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":adjust-log-time +1h" \
    -c ":reset-session" \
    ${test_dir}/logfile_java.*

# hiding fields failed
rm -rf ./sessions
mkdir -p $HOME
run_cap_test ${lnav_test} -nq -d /tmp/lnav.err \
    -c ":hide-fields c_ip" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# restoring hidden fields failed
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":test-comment restoring hidden fields" \
    ${test_dir}/logfile_access_log.0

# hiding fields failed
rm -rf ./sessions
mkdir -p $HOME
run_cap_test ${lnav_test} -nq -d /tmp/lnav.err \
    -c ":hide-lines-before 2009-07-20 22:59:29" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# XXX we don't actually check
# restoring hidden fields failed
run_cap_test ${lnav_test} -n -d /tmp/lnav.err \
    -c ":load-session" \
    -c ":test-comment restore hidden lines" \
    ${test_dir}/logfile_access_log.0

# hiding fields failed
rm -rf ./sessions
mkdir -p $HOME
run_cap_test ${lnav_test} -n \
    -c ":hide-fields bro_uid" \
    -c ":goto -10" \
    -c ":save-session" \
    ${test_dir}/logfile_bro_http.log.0

# restoring hidden fields failed
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ":goto -10" \
    -c ":test-comment restoring hidden fields" \
    ${test_dir}/logfile_bro_http.log.0

export TEST_ANNO=1
run_cap_test ${lnav_test} -d /tmp/lnav.err -I ${test_dir} -n \
    -c ':annotate' \
    -c ':save-session' \
    support-dump/logfile_access_log.0

run_cap_test ${lnav_test} -d /tmp/lnav.err -I ${test_dir} -n \
    -c ':load-session' \
    -c ':export-session-to -' \
    support-dump/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":hide-lines-before 4pm" \
    -c ":save-session" \
    ${test_dir}/logfile_w3c_big.0

run_cap_test ${lnav_test} -n \
    -c ":hide-lines-before 10pm" \
    -c ":load-session" \
    ${test_dir}/logfile_w3c_big.0

export YES_COLOR=1
rm -rf ./sessions
mkdir -p $HOME

run_cap_test ${lnav_test} -n \
    -c ":mark-expr :sc_bytes < 1000" \
    -c ":save-session" \
    ${test_dir}/logfile_w3c_big.0

run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    ${test_dir}/logfile_w3c_big.0

cp ${test_dir}/logfile_postgres.0 support-dump/
run_cap_test ${lnav_test} -nq \
    -c ":create-search-table pg_users user=(?<user_name>\w+)" \
    -c ":export-session-to -" \
    support-dump/logfile_postgres.0

rm -rf ./sessions
mkdir -p $HOME

# breakpoint saved in session
run_cap_test ${lnav_test} -nq \
    -c ":breakpoint logging_unittest.cc:259" \
    -c ":save-session" \
    ${test_dir}/logfile_glog.0

# breakpoint restored from session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";SELECT description FROM lnav_log_breakpoints" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_glog.0

rm -rf ./sessions
mkdir -p $HOME

# tag filter-expr saved in session
run_cap_test ${lnav_test} -nq \
    -c ":goto 0" \
    -c ":tag #bad" \
    -c ":filter-expr not json_contains(:log_tags, '#bad')" \
    -c ":save-session" \
    ${test_dir}/logfile_access_log.0

# tag filter-expr restored from session
run_cap_test ${lnav_test} -n \
    -c ":load-session" \
    -c ";SELECT log_line, log_tags FROM access_log" \
    -c ":write-csv-to -" \
    ${test_dir}/logfile_access_log.0

rm -rf ./sessions
mkdir -p $HOME

# user bookmark saved in session for timeline view
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':goto 0' \
    -c ':mark' \
    -c ':save-session' \
    ${test_dir}/logfile_vmw_log.0

# user bookmark restored from session for timeline view
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':switch-to-view timeline' \
    ${test_dir}/logfile_vmw_log.0

rm -rf ./sessions
mkdir -p $HOME

# sticky header saved in session for timeline view
run_cap_test ${lnav_test} -n \
    -c ':switch-to-view timeline' \
    -c ':goto 0' \
    -c ':toggle-sticky-header' \
    -c ':goto 1' \
    -c ':save-session' \
    ${test_dir}/logfile_vmw_log.0

# sticky header restored from session for timeline view
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':switch-to-view timeline' \
    -c ':goto 1' \
    ${test_dir}/logfile_vmw_log.0

rm -rf ./sessions
mkdir -p $HOME

# format-defined partitions should not be saved in the session
run_cap_test ${lnav_test} -nq \
    -I ${test_dir} \
    -c ':save-session' \
    ${test_dir}/logfile_partitions.0

run_cap_test sqlite3 ${HOME}/.lnav/log_metadata.db \
    "SELECT count(*) FROM bookmarks WHERE part_name IS NOT NULL AND part_name != ''"

# format-defined partitions should survive a session reset
run_cap_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ':reset-session' \
    -c ';SELECT log_line, log_part FROM syslog_log WHERE log_part IS NOT NULL' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_partitions.0

rm -rf ./sessions
mkdir -p $HOME

# sticky header saved in session for text file
run_cap_test ${lnav_test} -n \
    -c ':goto 1' \
    -c ':toggle-sticky-header' \
    -c ':save-session' \
    ${test_dir}/textfile_plain.0

# sticky header restored from session for text file
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':goto 4' \
    ${test_dir}/textfile_plain.0

rm -rf ./sessions
mkdir -p $HOME
mkdir -p support-bookmarks

# Markdown bookmark survives lines inserted above the section -- the
# stored anchor pivots the hash scan to the section's new position.
cat > support-bookmarks/anchored.md <<'EOF'
# Document

## Alpha

Line A1.
Line A2.

## Beta

Line B1.
Line B2.

## Gamma

Line G1.
EOF

# Save: mark "Line B1." (line 9) inside section "## Beta".
run_cap_test ${lnav_test} -nq \
    -c ':goto 9' \
    -c ':mark' \
    -c ':save-session' \
    support-bookmarks/anchored.md

# Insert five lines at the top of the file.
{ printf '%s\n' '## Intro' '' 'Prologue A.' 'Prologue B.' ''; \
    cat support-bookmarks/anchored.md; } \
    > support-bookmarks/anchored.md.new
mv support-bookmarks/anchored.md.new support-bookmarks/anchored.md

# Reload: bookmark should follow "Line B1." to its new line 14
# (9 + 5 inserted lines).
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':goto 0' \
    -c ':next-mark user' \
    -c ";SELECT selection FROM lnav_views WHERE name = 'text'" \
    -c ':write-csv-to -' \
    support-bookmarks/anchored.md

rm -rf ./sessions
mkdir -p $HOME

# Plain-text bookmark whose content was rewritten falls back to the
# saved line number rather than being dropped silently.
cat > support-bookmarks/plain.txt <<'EOF'
apple
banana
cherry
date
elderberry
fig
EOF

run_cap_test ${lnav_test} -nq \
    -c ':goto 2' \
    -c ':mark' \
    -c ':save-session' \
    support-bookmarks/plain.txt

# Rewrite every line so hashes never match anywhere.
cat > support-bookmarks/plain.txt <<'EOF'
one
two
three
four
five
six
EOF

# Reload: bookmark falls back to saved line number 2.
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':goto 0' \
    -c ':next-mark user' \
    -c ";SELECT selection FROM lnav_views WHERE name = 'text'" \
    -c ':write-csv-to -' \
    support-bookmarks/plain.txt

rm -rf ./sessions
mkdir -p $HOME

# Markdown bookmark survives content shift INSIDE its anchored
# section -- anchor resolves but the saved offset is now stale, so
# the hash scan has to walk outward from the pivot to find a match.
cat > support-bookmarks/shifted.md <<'EOF'
# Document

## Alpha

Line A1.
Line A2.

## Beta

Line B1.
Line B2.

## Gamma

Line G1.
EOF

# Save: mark "Line B1." (line 9) inside section "## Beta".
run_cap_test ${lnav_test} -nq \
    -c ':goto 9' \
    -c ':mark' \
    -c ':save-session' \
    support-bookmarks/shifted.md

# Insert two lines inside "## Beta" before "Line B1." -- the
# section's anchor row is unchanged but the bookmarked line moves
# from saved-offset 2 to saved-offset 4 within the section.
cat > support-bookmarks/shifted.md <<'EOF'
# Document

## Alpha

Line A1.
Line A2.

## Beta

Extra line.
Another line.
Line B1.
Line B2.

## Gamma

Line G1.
EOF

# Reload: pivot = anchor_row (7) + saved_offset (2) = 9, which now
# holds "Extra line." The hash scan should find "Line B1." at the
# new line 11.
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':goto 0' \
    -c ':next-mark user' \
    -c ";SELECT selection FROM lnav_views WHERE name = 'text'" \
    -c ':write-csv-to -' \
    support-bookmarks/shifted.md

rm -rf ./sessions
mkdir -p $HOME

# Markdown bookmark whose anchor heading was deleted: anchor no
# longer resolves, but the stored line_number serves as the pivot
# and the hash scan locates the line at its new position.
cat > support-bookmarks/no_anchor.md <<'EOF'
# Document

## Alpha

Line A1.
Line A2.

## Beta

Line B1.
Line B2.

## Gamma

Line G1.
EOF

run_cap_test ${lnav_test} -nq \
    -c ':goto 9' \
    -c ':mark' \
    -c ':save-session' \
    support-bookmarks/no_anchor.md

# Remove the "## Beta" heading entirely.  "Line B1." shifts up by
# one to line 8.  The "#beta" anchor won't resolve on reload.
cat > support-bookmarks/no_anchor.md <<'EOF'
# Document

## Alpha

Line A1.
Line A2.


Line B1.
Line B2.

## Gamma

Line G1.
EOF

# Reload: anchor lookup fails -> pivot falls back to saved line 9.
# Hash scan finds "Line B1." at the new line 8.
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':goto 0' \
    -c ':next-mark user' \
    -c ";SELECT selection FROM lnav_views WHERE name = 'text'" \
    -c ':write-csv-to -' \
    support-bookmarks/no_anchor.md

rm -rf ./sessions
mkdir -p $HOME

# Plain-text bookmark with a small edit above: no anchor, but the
# hash scan must shift the bookmark to follow the inserted line.
cat > support-bookmarks/small_edit.txt <<'EOF'
apple
banana
cherry
date
elderberry
fig
EOF

run_cap_test ${lnav_test} -nq \
    -c ':goto 2' \
    -c ':mark' \
    -c ':save-session' \
    support-bookmarks/small_edit.txt

# Insert one line at the top so "cherry" shifts from line 2 to 3.
{ printf '%s\n' 'avocado'; cat support-bookmarks/small_edit.txt; } \
    > support-bookmarks/small_edit.txt.new
mv support-bookmarks/small_edit.txt.new support-bookmarks/small_edit.txt

# Reload: no anchor info, so pivot = saved line 2; hash scan finds
# "cherry" at the new line 3.
run_cap_test ${lnav_test} -n \
    -c ':load-session' \
    -c ':goto 0' \
    -c ':next-mark user' \
    -c ";SELECT selection FROM lnav_views WHERE name = 'text'" \
    -c ':write-csv-to -' \
    support-bookmarks/small_edit.txt

