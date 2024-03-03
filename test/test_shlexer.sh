#! /bin/bash

export FOO='bar'
export DEF='xyz'

run_cap_test ./drive_shlexer '$FOO'

run_cap_test ./drive_shlexer '${FOO}'

# run_cap_test ./drive_shlexer '\a'

run_cap_test ./drive_shlexer '\'

run_cap_test ./drive_shlexer "'abc'"

run_cap_test ./drive_shlexer '"def"'

run_cap_test ./drive_shlexer '"'"'"'"'

run_cap_test ./drive_shlexer "'"'"'"'"

run_cap_test ./drive_shlexer '"abc $DEF 123"'

run_cap_test ./drive_shlexer '"abc ${DEF} 123"'

run_cap_test ./drive_shlexer "'abc \$DEF 123'"

run_cap_test ./drive_shlexer 'abc $DEF  123'

run_cap_test ./drive_shlexer '~ foo'

run_cap_test ./drive_shlexer '~nonexistent/bar baz'

run_cap_test ./drive_shlexer 'abc '
