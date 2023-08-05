#! /bin/bash

run_cap_test ./drive_sql "select readlink('non-existent-link')"

ln -sf sql_fs_readlink_test sql_fs_readlink_test.lnk
run_cap_test ./drive_sql "select readlink('sql_fs_readlink_test.lnk')"
rm sql_fs_readlink_test.lnk

run_cap_test ./drive_sql "select realpath('non-existent-path')"

# ln -sf drive_sql sql_fs_realpath_test.lnk
# run_cap_test ./drive_sql "select realpath('sql_fs_realpath_test.lnk')"
# rm sql_fs_realpath_test.lnk

run_cap_test ./drive_sql "select basename('')"

run_cap_test ./drive_sql "select basename('/')"

run_cap_test ./drive_sql "select basename('//')"

run_cap_test ./drive_sql "select basename('/foo')"

run_cap_test ./drive_sql "select basename('foo/bar')"

run_cap_test ./drive_sql "select basename('/foo/')"

run_cap_test ./drive_sql "select basename('/foo///')"

run_cap_test ./drive_sql "select basename('foo')"

run_cap_test ./drive_sql "select dirname('')"

run_cap_test ./drive_sql "select dirname('foo')"

run_cap_test ./drive_sql "select dirname('foo///')"

run_cap_test ./drive_sql "select dirname('/foo/bar')"

run_cap_test ./drive_sql "select dirname('/')"

run_cap_test ./drive_sql "select dirname('/foo')"

run_cap_test ./drive_sql "select dirname('/foo//')"

run_cap_test ./drive_sql "select dirname('foo//')"

run_cap_test ./drive_sql "select joinpath()"

run_cap_test ./drive_sql "select joinpath('foo')"

run_cap_test ./drive_sql "select joinpath('foo', 'bar', 'baz')"

run_cap_test ./drive_sql "select joinpath('foo', 'bar', 'baz', '/root')"

run_cap_test ${lnav_test} -Nn -c ";SELECT shell_exec('echo hi')"
