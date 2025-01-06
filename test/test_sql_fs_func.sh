#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

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

run_cap_test ${lnav_test} -Nn -c ";SELECT shell_exec('cat', 'hi')"

run_cap_test ${lnav_test} -Nn -c ";SELECT shell_exec('echo hi', NULL, '{ 1')"

run_cap_test ${lnav_test} -Nn \
    -c ";SELECT shell_exec('echo \$msg', NULL, json_object('env', json_object('msg', 'hi')))"

run_cap_test ${lnav_test} -Nn -c ";SELECT * FROM fstat('/non-existent')"

run_cap_test ${lnav_test} -Nn -c ";SELECT * FROM fstat('/*.non-existent')"

echo "Hello, World!" > fstat-hw.dat
touch -t 200711030923 fstat-hw.dat
chmod 0644 fstat-hw.dat
run_cap_test ${lnav_test} -Nn -c ";SELECT st_name,st_type,st_mode,st_nlink,st_size,st_mtime,error,data FROM fstat('fstat-hw.dat')"
