#! /bin/bash

# Tests for the HTTP server opened by :external-access.  lnav is started in
# the background, found through its discovery file, and stopped before any of
# the checks run, since a failed check exits the script.

# Base64 of "test-key", which is what the server expects in the header.
api_key_header="X-Api-Key: dGVzdC1rZXk="

# Installed scripts that requests without the "external" effect may run.
mkdir -p ${HOME}/.lnav/formats/installed
printf ':echo from an installed script\n' \
    > ${HOME}/.lnav/formats/installed/ext_access_echo.lnav
printf ':filter-out GET\n' \
    > ${HOME}/.lnav/formats/installed/ext_access_installed_hide.lnav

${lnav_test} -n \
    -c ":external-access 0 test-key" \
    ${test_dir}/logfile_access_log.0 \
    > ext_access_lnav.out 2> ext_access_lnav.err &
lnav_pid=$!

url=""
for lpc in $(seq 1 40); do
    url=$(${lnav_test} -m instances list -o url 2> /dev/null | head -1)
    if test x"${url}" != x""; then
        break
    fi
    if ! kill -0 ${lnav_pid} 2> /dev/null; then
        break
    fi
    sleep 0.25
done

if test x"${url}" = x""; then
    kill ${lnav_pid} 2> /dev/null
    wait ${lnav_pid} 2> /dev/null
    if grep -q "without Rust" ext_access_lnav.err; then
        echo "skipping, lnav was compiled without Rust extensions"
        exit 0
    fi
    echo "lnav did not open an external-access port"
    cat ext_access_lnav.err
    exit 1
fi

curl -s -o ext_access_theme.css \
    -w '%{http_code} %{content_type}\n' \
    -H "${api_key_header}" \
    "${url}/assets/css/theme.css" > ext_access_theme.status

curl -s -o /dev/null \
    -w '%{http_code}\n' \
    "${url}/assets/css/theme.css" > ext_access_no_key.status

curl -s -o ext_access_page.html \
    -H "${api_key_header}" \
    "${url}/apps/lnav/api-test/"

# Runs a script with the given X-Lnav-Allow-Effects header ("none" leaves the
# header out) and appends the response to ext_access_effects.out.
exec_with_effects() {
    local effects="$1"
    local script="$2"
    local effects_header=()

    if test x"${effects}" != x"none"; then
        effects_header=(-H "X-Lnav-Allow-Effects: ${effects}")
    fi
    echo "--- ${effects}: ${script}" | head -1 >> ext_access_effects.out
    curl -s \
        -H "${api_key_header}" \
        -H "Content-Type: text/x-lnav-script" \
        "${effects_header[@]}" \
        --data-binary "${script}" \
        "${url}/api/exec" >> ext_access_effects.out
    echo >> ext_access_effects.out
}

rm -f ext_access_effects.out ext_access_effects.csv ext_access_effects.sql \
    ext_access_attach.db ext_access_late.db
# commands are checked against the allowed effects, also when run by :eval
exec_with_effects "read, output" $';SELECT count(*) AS n FROM lnav_view_filters\n:write-csv-to -\n'
exec_with_effects "read, output" $':filter-out GET\n'
exec_with_effects "read,output,view" $':filter-out GET\n;SELECT count(*) AS n FROM lnav_view_filters\n:write-csv-to -\n'
exec_with_effects "read,output,view" $';DELETE FROM lnav_view_filters\n'
exec_with_effects "read,output" $':quit\n'
exec_with_effects "read,output" $':eval :echo hi\n'
exec_with_effects "read,output" $':eval :filter-out GET\n'
exec_with_effects "read,output,view" $':eval :sh echo hi\n'
# a script file that isn't installed needs "external", since running it reads
# the file; an installed script runs, though its commands are still checked
printf ':filter-out GET\n' > ext_access_hide.lnav
printf ':echo from a script file\n' > ext_access_echo_file.lnav
exec_with_effects "read,output" $'|ext_access_hide.lnav\n'
exec_with_effects "read,output" $'|/etc/hosts\n'
exec_with_effects "read,output,external" $'|ext_access_echo_file.lnav\n'
exec_with_effects "read,output" $'|ext_access_echo\n'
exec_with_effects "read,output" $'|ext_access_installed_hide\n'
# SQL that changes data needs "view"
exec_with_effects "read,output" $';UPDATE lnav_view_filters SET enabled = 0\n'
# so does a search
exec_with_effects "read,output" $'/GET\n'
# writing to a file instead of the output, and SQL that reaches outside of
# lnav, need "external", even inside another command's SQL
exec_with_effects "read,output" $';SELECT 1 AS n\n:write-csv-to ext_access_effects.csv\n'
exec_with_effects "read,output,view,config" $';.dump ext_access_effects.sql lnav_help\n'
exec_with_effects "read,output" $';SELECT shell_exec(\'echo hi\') AS out\n:write-csv-to -\n'
exec_with_effects "read,output,view" $':filter-expr shell_exec(\'echo hi\') IS NOT NULL\n'
exec_with_effects "read,output" $';SELECT * FROM fstat(\'/\')\n:write-csv-to -\n'
exec_with_effects "read,output,view" $';ATTACH \'ext_access_attach.db\' AS ext_db\n'
exec_with_effects "read,output,view" $';VACUUM INTO \'ext_access_attach.db\'\n'
exec_with_effects "read,output,view" $';CREATE TEMP TRIGGER ext_trig AFTER INSERT ON lnav_events BEGIN SELECT 1; END\n'
exec_with_effects "read,output,view" $';CREATE TEMP VIEW lnav_file AS SELECT shell_exec(\'echo hi\') AS content_id\n'
exec_with_effects "read,output,view" $';PRAGMA writable_schema = 1\n'
exec_with_effects "read,output" $';SELECT name FROM pragma_table_info(\'lnav_view_filters\') LIMIT 2\n:write-csv-to -\n'
# an unknown effect fails the request; no header means no limit
exec_with_effects "read,bogus" $':echo hi\n'
exec_with_effects "none" $';SELECT count(*) AS n FROM lnav_view_filters\n:write-csv-to -\n'
# a database file that lnav finds while a limited request runs is still
# attached, since lnav does that itself
exec_with_effects "none" $':open ext_access_late*.db\n'
sqlite3 ext_access_late.db 'CREATE TABLE late_tbl (a)'
exec_with_effects "read,output,view" $':rebuild\n;SELECT name FROM pragma_database_list\n:write-csv-to -\n'

# An X-Lnav- header lnav doesn't know (from a later version, or misspelled), or a value it
# can't use, fails the request rather than being ignored; known ones match in any case.
rm -f ext_access_headers.out
for hdr in "X-Lnav-Allow-Effect: read" "X-Lnav-Dry-Run: 1" "X-Lnav-Db-View: sharde" \
    "x-lnav-allow-effects: read, output" "X-LNAV-DB-VIEW: SHARED" "X-Lnav-Var-n: MQ=="; do
    echo "--- ${hdr}" >> ext_access_headers.out
    curl -s -w ' %{http_code}\n' \
        -H "${api_key_header}" \
        -H "Content-Type: text/x-lnav-script" \
        -H "${hdr}" \
        --data-binary $':echo hi\n' \
        "${url}/api/exec" >> ext_access_headers.out
done
# A known header given twice fails, instead of using only the last value, even when the
# second one differs only in case.
for hdrs in "X-Lnav-Allow-Effects: read|X-Lnav-Allow-Effects: read, output, view, config" \
    "X-Lnav-Allow-Effects: read|x-lnav-allow-effects: read" \
    "X-Lnav-Db-View: shared|X-Lnav-Db-View: shared"; do
    echo "--- ${hdrs}" >> ext_access_headers.out
    curl -s -w ' %{http_code}\n' \
        -H "${api_key_header}" \
        -H "Content-Type: text/x-lnav-script" \
        -H "${hdrs%%|*}" \
        -H "${hdrs#*|}" \
        --data-binary $':echo hi\n' \
        "${url}/api/exec" >> ext_access_headers.out
done
echo "--- poll with X-Lnav-Foo" >> ext_access_headers.out
curl -s -o /dev/null -w '%{http_code}\n' \
    -H "${api_key_header}" \
    -H "Content-Type: application/json" \
    -H "X-Lnav-Foo: 1" \
    --data-binary 'null' \
    "${url}/api/poll" >> ext_access_headers.out

kill ${lnav_pid} 2> /dev/null
wait ${lnav_pid} 2> /dev/null

run_test cat ext_access_theme.status

check_output "theme.css is not served as CSS?" <<EOF
200 text/css; charset=utf8
EOF

run_test grep -c '^\.-lnav_styles_error {' ext_access_theme.css

check_output "theme.css does not have the rules for the theme?" <<EOF
1
EOF

run_test cat ext_access_no_key.status

check_output "theme.css is served without an API key?" <<EOF
401
EOF

run_test grep -c 'href="/assets/css/theme.css"' ext_access_page.html

check_output "rendered pages do not link to theme.css?" <<EOF
1
EOF

run_test cat ext_access_effects.out

check_output "effects are not limited by X-Lnav-Allow-Effects?" <<EOF
--- read, output: ;SELECT count(*) AS n FROM lnav_view_filters
n
0

--- read, output: :filter-out GET
{"type":"effect-refused","msg":":filter-out is not allowed here","reason":"its effect is “view”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: :filter-out GET
n
1

--- read,output,view: ;DELETE FROM lnav_view_filters

--- read,output: :quit
{"type":"effect-refused","msg":":quit is not allowed here","reason":"its effect is “exit”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: :eval :echo hi
hi

--- read,output: :eval :filter-out GET
{"type":"effect-refused","msg":":filter-out is not allowed here","reason":"its effect is “view”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: :eval :sh echo hi
{"type":"effect-refused","msg":":sh is not allowed here","reason":"its effect is “external”, and only these are allowed: read, output, view","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: |ext_access_hide.lnav
{"type":"effect-refused","msg":"|ext_access_hide.lnav is not allowed here","reason":"it is not an installed script, so its effect is “external”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: |/etc/hosts
{"type":"effect-refused","msg":"|/etc/hosts is not allowed here","reason":"it is not an installed script, so its effect is “external”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,external: |ext_access_echo_file.lnav
from a script file

--- read,output: |ext_access_echo
from an installed script

--- read,output: |ext_access_installed_hide
{"type":"effect-refused","msg":":filter-out is not allowed here","reason":"its effect is “view”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: ;UPDATE lnav_view_filters SET enabled = 0
{"type":"effect-refused","msg":"an SQL statement that changes data is not allowed here","reason":"its effect is “view”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: /GET
{"type":"effect-refused","msg":"a search is not allowed here","reason":"its effect is “view”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: ;SELECT 1 AS n
{"type":"effect-refused","msg":":write-csv-to ext_access_effects.csv is not allowed here","reason":"its effect is “external”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view,config: ;.dump ext_access_effects.sql lnav_help
{"type":"effect-refused","msg":".dump is not allowed here","reason":"its effect is “external”, and only these are allowed: read, output, view, config","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: ;SELECT shell_exec('echo hi') AS out
{"type":"effect-refused","msg":"shell_exec() is not allowed here","reason":"it runs a program, so its effect is “external”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: :filter-expr shell_exec('echo hi') IS NOT NULL
{"type":"effect-refused","msg":"shell_exec() is not allowed here","reason":"it runs a program, so its effect is “external”, and only these are allowed: read, output, view","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: ;SELECT * FROM fstat('/')
{"type":"effect-refused","msg":"fstat() is not allowed here","reason":"it reads the file system, so its effect is “external”, and only these are allowed: read, output","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: ;ATTACH 'ext_access_attach.db' AS ext_db
{"type":"effect-refused","msg":"ATTACH is not allowed here","reason":"it opens a database file, so its effect is “external”, and only these are allowed: read, output, view","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: ;VACUUM INTO 'ext_access_attach.db'
{"type":"effect-refused","msg":"ATTACH is not allowed here","reason":"it opens a database file, so its effect is “external”, and only these are allowed: read, output, view","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: ;CREATE TEMP TRIGGER ext_trig AFTER INSERT ON lnav_events BEGIN SELECT 1; END
{"type":"effect-refused","msg":"CREATE TRIGGER is not allowed here","reason":"the trigger would run later, without this request's limits, so its effect is “external”, and only these are allowed: read, output, view","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: ;CREATE TEMP VIEW lnav_file AS SELECT shell_exec('echo hi') AS content_id
{"type":"effect-refused","msg":"CREATE VIEW is not allowed here","reason":"the view would run later, without this request's limits, so its effect is “external”, and only these are allowed: read, output, view","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output,view: ;PRAGMA writable_schema = 1
{"type":"effect-refused","msg":"PRAGMA writable_schema = ... is not allowed here","reason":"it changes a setting, so its effect is “external”, and only these are allowed: read, output, view","help":"the X-Lnav-Allow-Effects header of an external-access request lists the effects its commands may have; see the effect column of the lnav_help table"}
--- read,output: ;SELECT name FROM pragma_table_info('lnav_view_filters') LIMIT 2
name
view_name
filter_id

--- read,bogus: :echo hi
{"type":"bad-request-header","msg":"unknown effect in X-Lnav-Allow-Effects: bogus","reason":"","help":"the effects are listed in the effect column of the lnav_help table"}
--- none: ;SELECT count(*) AS n FROM lnav_view_filters
n
0

--- none: :open ext_access_late*.db

--- read,output,view: :rebuild
name
main
lnav_db
ext_access_late_db

EOF

run_test ls ext_access_effects.csv ext_access_effects.sql ext_access_attach.db

check_output "a refused command wrote a file?" <<EOF
EOF

run_test cat ext_access_headers.out

check_output "unknown X-Lnav- headers are not refused?" <<EOF
--- X-Lnav-Allow-Effect: read
{"help":"this lnav reads these X-Lnav- headers: X-Lnav-Db-View, X-Lnav-Allow-Effects, and X-Lnav-Var-<name>","msg":"unknown header: X-Lnav-Allow-Effect","reason":"lnav would ignore it"} 400
--- X-Lnav-Dry-Run: 1
{"help":"this lnav reads these X-Lnav- headers: X-Lnav-Db-View, X-Lnav-Allow-Effects, and X-Lnav-Var-<name>","msg":"unknown header: X-Lnav-Dry-Run","reason":"lnav would ignore it"} 400
--- X-Lnav-Db-View: sharde
{"help":"the only value is \"shared\"","msg":"invalid value for X-Lnav-Db-View: sharde","reason":"lnav would ignore the header"} 400
--- x-lnav-allow-effects: read, output
hi
 200
--- X-LNAV-DB-VIEW: SHARED
hi
 200
--- X-Lnav-Var-n: MQ==
hi
 200
--- X-Lnav-Allow-Effects: read|X-Lnav-Allow-Effects: read, output, view, config
{"help":"send the header once, with all of its values","msg":"header given more than once: X-Lnav-Allow-Effects","reason":"lnav would use only one of the values"} 400
--- X-Lnav-Allow-Effects: read|x-lnav-allow-effects: read
{"help":"send the header once, with all of its values","msg":"header given more than once: x-lnav-allow-effects","reason":"lnav would use only one of the values"} 400
--- X-Lnav-Db-View: shared|X-Lnav-Db-View: shared
{"help":"send the header once, with all of its values","msg":"header given more than once: X-Lnav-Db-View","reason":"lnav would use only one of the values"} 400
--- poll with X-Lnav-Foo
400
EOF
