#! /bin/bash

export FOO='bar'
export DEF='xyz'

run_test ./drive_shlexer '$FOO'

check_output_ws "var ref" <<EOF
    \$FOO
ref ^--^
eval -- bar
split:
  0 -- bar
EOF

run_test ./drive_shlexer '${FOO}'

check_output_ws "var ref" <<EOF
    \${FOO}
qrf ^----^
eval -- bar
split:
  0 -- bar
EOF

run_test ./drive_shlexer '\a'

check_output_ws "escape" <<EOF
    \a
esc ^^
eval -- a
split:
  0 -- a
EOF

run_test ./drive_shlexer '\'

check_output_ws "error" <<EOF
    \\
err ^
EOF

run_test ./drive_shlexer "'abc'"

check_output_ws "single" <<EOF
    'abc'
sst ^
sen     ^
eval -- 'abc'
split:
  0 -- abc
EOF

run_test ./drive_shlexer '"def"'

check_output_ws "double" <<EOF
    "def"
dst ^
den     ^
eval -- "def"
split:
  0 -- def
EOF

run_test ./drive_shlexer '"'"'"'"'

check_output_ws "double with single" <<EOF
    "'"
dst ^
den   ^
eval -- "'"
split:
  0 -- '
EOF

run_test ./drive_shlexer "'"'"'"'"

check_output_ws "single with double" <<EOF
    '"'
sst ^
sen   ^
eval -- '"'
split:
  0 -- "
EOF

run_test ./drive_shlexer '"abc $DEF 123"'

check_output_ws "double w/ref" <<EOF
    "abc \$DEF 123"
dst ^
ref      ^--^
den              ^
eval -- "abc xyz 123"
split:
  0 -- abc xyz 123
EOF

run_test ./drive_shlexer '"abc ${DEF} 123"'

check_output_ws "double w/quoted-ref" <<EOF
    "abc \${DEF} 123"
dst ^
qrf      ^----^
den                ^
eval -- "abc xyz 123"
split:
  0 -- abc xyz 123
EOF

run_test ./drive_shlexer "'abc \$DEF 123'"

check_output_ws "single w/ref" <<EOF
    'abc \$DEF 123'
sst ^
sen              ^
eval -- 'abc \$DEF 123'
split:
  0 -- abc \$DEF 123
EOF

run_test ./drive_shlexer 'abc $DEF  123'

check_output_ws "unquoted" <<EOF
    abc \$DEF  123
wsp    ^
ref     ^--^
wsp         ^^
eval -- abc xyz  123
split:
  0 -- abc
  1 -- xyz
  2 -- 123
EOF

run_test ./drive_shlexer '~ foo'

check_output_ws "tilde" <<EOF
    ~ foo
til ^
wsp  ^
eval -- ../test foo
split:
  0 -- ../test
  1 -- foo
EOF

run_test ./drive_shlexer '~nonexistent/bar baz'

check_output_ws "tilde with username" <<EOF
    ~nonexistent/bar baz
til ^----------^
wsp                 ^
eval -- ~nonexistent/bar baz
split:
  0 -- ~nonexistent/bar
  1 -- baz
EOF
