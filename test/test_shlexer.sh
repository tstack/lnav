#! /bin/bash

export FOO='bar'
export DEF='xyz'

run_test ./drive_shlexer '$FOO'

check_output "var ref" <<EOF
    \$FOO
ref ^--^
eval -- bar
EOF

run_test ./drive_shlexer '\a'

check_output "escape" <<EOF
    \a
esc ^^
eval -- a
EOF

run_test ./drive_shlexer "'abc'"

check_output "single" <<EOF
    'abc'
sst ^
sen     ^
eval -- abc
EOF

run_test ./drive_shlexer '"def"'

check_output "double" <<EOF
    "def"
dst ^
den     ^
eval -- def
EOF

run_test ./drive_shlexer '"abc $DEF 123"'

check_output "double w/ref" <<EOF
    "abc \$DEF 123"
dst ^
ref      ^--^
den              ^
eval -- abc xyz 123
EOF

run_test ./drive_shlexer "'abc \$DEF 123'"

check_output "single w/ref" <<EOF
    'abc \$DEF 123'
sst ^
sen              ^
eval -- abc \$DEF 123
EOF

run_test ./drive_shlexer 'abc $DEF 123'

check_output "unquoted" <<EOF
    abc \$DEF 123
ref     ^--^
eval -- abc xyz 123
EOF
