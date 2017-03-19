#! /bin/bash

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
