#! /bin/bash

run_test ./drive_sql "select readlink('non-existent-link')"

check_error_output "readlink() with invalid path works?" <<EOF
error: sqlite3_exec failed -- unable to stat path: non-existent-link -- No such file or directory
EOF

ln -sf sql_fs_readlink_test sql_fs_readlink_test.lnk
run_test ./drive_sql "select readlink('sql_fs_readlink_test.lnk')"
rm sql_fs_readlink_test.lnk

check_output "readlink() does not work?" <<EOF
Row 0:
  Column readlink('sql_fs_readlink_test.lnk'): sql_fs_readlink_test
EOF

run_test ./drive_sql "select realpath('non-existent-path')"

check_error_output "realpath() with invalid path works?" <<EOF
error: sqlite3_exec failed -- Could not get real path for non-existent-path -- No such file or directory
EOF

ln -sf drive_sql sql_fs_realpath_test.lnk
run_test ./drive_sql "select realpath('sql_fs_realpath_test.lnk')"
rm sql_fs_realpath_test.lnk

sed -e "s|${builddir}|<build_dir>|g" `test_filename` > test_realpath.out
mv test_realpath.out `test_filename`
check_output "realpath() does not work?" <<EOF
Row 0:
  Column realpath('sql_fs_realpath_test.lnk'): <build_dir>/drive_sql
EOF

run_test ./drive_sql "select basename('')"

check_output "basename('') is not '.'" <<EOF
Row 0:
  Column basename(''): .
EOF

run_test ./drive_sql "select basename('/')"

check_output "basename('/') is not '/'" <<EOF
Row 0:
  Column basename('/'): /
EOF

run_test ./drive_sql "select basename('//')"

check_output "basename('//') is not '/'" <<EOF
Row 0:
  Column basename('//'): /
EOF

run_test ./drive_sql "select basename('/foo')"

check_output "basename('/foo') is not 'foo'" <<EOF
Row 0:
  Column basename('/foo'): foo
EOF

run_test ./drive_sql "select basename('foo/bar')"

check_output "basename('foo/bar') is not 'bar'" <<EOF
Row 0:
  Column basename('foo/bar'): bar
EOF

run_test ./drive_sql "select basename('/foo/')"

check_output "basename('/foo/') is not 'foo'" <<EOF
Row 0:
  Column basename('/foo/'): foo
EOF

run_test ./drive_sql "select basename('/foo///')"

check_output "basename('/foo///') is not 'foo'" <<EOF
Row 0:
  Column basename('/foo///'): foo
EOF

run_test ./drive_sql "select basename('foo')"

check_output "basename('foo') is not 'foo'" <<EOF
Row 0:
  Column basename('foo'): foo
EOF


run_test ./drive_sql "select dirname('')"

check_output "dirname('') is not '.'" <<EOF
Row 0:
  Column dirname(''): .
EOF

run_test ./drive_sql "select dirname('foo')"

check_output "dirname('foo') is not '.'" <<EOF
Row 0:
  Column dirname('foo'): .
EOF

run_test ./drive_sql "select dirname('foo///')"

check_output "dirname('foo///') is not '.'" <<EOF
Row 0:
  Column dirname('foo///'): .
EOF

run_test ./drive_sql "select dirname('/foo/bar')"

check_output "dirname('/foo/bar') is not '/foo'" <<EOF
Row 0:
  Column dirname('/foo/bar'): /foo
EOF

run_test ./drive_sql "select dirname('/')"

check_output "dirname('/') is not '/'" <<EOF
Row 0:
  Column dirname('/'): /
EOF

run_test ./drive_sql "select dirname('/foo')"

check_output "dirname('/foo') is not '/'" <<EOF
Row 0:
  Column dirname('/foo'): /
EOF

run_test ./drive_sql "select dirname('/foo//')"

check_output "dirname('/foo//') is not '/'" <<EOF
Row 0:
  Column dirname('/foo//'): /
EOF

run_test ./drive_sql "select dirname('foo//')"

check_output "dirname('foo//') is not '.'" <<EOF
Row 0:
  Column dirname('foo//'): .
EOF


run_test ./drive_sql "select joinpath()"

check_output "joinpath() is not ''" <<EOF
Row 0:
  Column joinpath(): (null)
EOF

run_test ./drive_sql "select joinpath('foo')"

check_output "joinpath('foo') is not ''" <<EOF
Row 0:
  Column joinpath('foo'): foo
EOF

run_test ./drive_sql "select joinpath('foo', 'bar', 'baz')"

check_output "joinpath('foo', 'bar', 'baz') is not ''" <<EOF
Row 0:
  Column joinpath('foo', 'bar', 'baz'): foo/bar/baz
EOF

run_test ./drive_sql "select joinpath('foo', 'bar', 'baz', '/root')"

check_output "joinpath('foo', 'bar', 'baz', '/root') is not ''" <<EOF
Row 0:
  Column joinpath('foo', 'bar', 'baz', '/root'): /root
EOF
