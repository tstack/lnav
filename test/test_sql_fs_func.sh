#! /bin/bash

export TZ=UTC
export YES_COLOR=1
unset XDG_CONFIG_HOME

run_cap_test ./drive_sql "select readlink('non-existent-link')"

run_cap_test ./drive_sql "select readlink('drive_sql')"

ln -sf sql_fs_readlink_test sql_fs_readlink_test.lnk
run_cap_test ./drive_sql "select readlink('sql_fs_readlink_test.lnk')"
rm sql_fs_readlink_test.lnk

run_cap_test ./drive_sql "select realpath('non-existent-path')"

# the resolved path depends on the build directory, so only check parts of it
ln -sf drive_sql sql_fs_realpath_test.lnk
run_cap_test ./drive_sql "select basename(realpath('sql_fs_realpath_test.lnk')), realpath('sql_fs_realpath_test.lnk') = realpath('drive_sql'), substr(realpath('.'), 1, 1)"
rm sql_fs_realpath_test.lnk

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

run_cap_test ./drive_sql "select dirname('foo//bar'), dirname('/foo//bar'), dirname('//bar')"

run_cap_test ./drive_sql "select joinpath()"

run_cap_test ./drive_sql "select joinpath('foo')"

run_cap_test ./drive_sql "select joinpath('foo', 'bar', 'baz')"

run_cap_test ./drive_sql "select joinpath('foo', 'bar', 'baz', '/groot')"

run_cap_test ${lnav_test} -Nn -c ";SELECT shell_exec('echo hi')"

run_cap_test ${lnav_test} -Nn -c ";SELECT shell_exec('cat', 'hi')"

run_cap_test ${lnav_test} -Nn -c ";SELECT shell_exec('echo hi', NULL, '{ 1')"

run_cap_test ${lnav_test} -Nn \
    -c ";SELECT shell_exec('echo \$msg', NULL, json_object('env', json_object('msg', 'hi')))"

# a NULL unsets the variable and the rest of the environment is passed through
run_cap_test ${lnav_test} -Nn \
    -c ";SELECT shell_exec('echo \"[\$TZ] [\$msg] [\$YES_COLOR]\"', NULL, json_object('env', json_object('TZ', NULL, 'msg', 'hi')))"

run_cap_test ${lnav_test} -Nn -c ";SELECT * FROM fstat('/non-existent')"

run_cap_test ${lnav_test} -Nn -c ";SELECT * FROM fstat('/*.non-existent')"

# a NULL pattern matches nothing
run_cap_test ${lnav_test} -Nn -c ";SELECT count(*) FROM fstat(NULL)"

echo "Hello, World!" > fstat-hw.dat
touch -t 200711030923 fstat-hw.dat
chmod 0644 fstat-hw.dat
run_cap_test ${lnav_test} -Nn -c ";SELECT st_name,st_type,st_mode,st_nlink,st_size,st_mtime,error,data FROM fstat('fstat-hw.dat')"

run_cap_test ${lnav_test} -n \
    -c ";SELECT filepath, st_size FROM lnav_file, fstat(filepath)" \
    ${test_dir}/logfile_access_log.*

# Only a "**" component can match more than one component of the path.
run_cap_test ./drive_sql "select path_match('/d/**/*.log', '/d/x.log') as zero_dirs, path_match('/d/**/*.log', '/d/a/b/c/x.log') as many_dirs, path_match('/d/**/c/*.log', '/d/a/b/x.log') as missing_dir, path_match('**/*.log', 'a/x.log') as relative"

run_cap_test ./drive_sql "select path_match('*.log', '/d/x.log') as star, path_match('/d/?/x.log', '/d/ab/x.log') as question, path_match('/d?x.log', '/d/x.log') as question_slash, path_match('/d[/]x.log', '/d/x.log') as bracket_slash, path_match('/d/[ab]/x.log', '/d/a/x.log') as bracket"

run_cap_test ./drive_sql "select path_match('/d/**/[', '/d/[') as invalid, path_match(NULL, '/d/x.log') as null_pattern, path_match('/d/*.log', NULL) as null_path"
