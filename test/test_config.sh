#!/usr/bin/env bash

export HOME="./test-config"
export XDG_CONFIG_HOME="./test-config/.config"
rm -rf ./test-config
mkdir -p $HOME/.config

run_test ${lnav_test} -nN \
    -c ":config /global/foo bar"

check_output "config write global var" <<EOF
EOF

run_test ${lnav_test} -nN \
    -c ":config /global/foo"

check_output "config read global var" <<EOF
/global/foo = "foo"
EOF

run_test ${lnav_test} -n \
    -c ":config /ui/theme-defs/default/styles/text/color #f" \
    ${test_dir}/logfile_access_log.0

check_error_output "config bad color" <<EOF
✘ error: invalid value for property “/ui/theme-defs/default/styles/text/color”
 reason: Could not parse color: #f
 --> command-option:1
EOF

run_test env TMPDIR=tmp ${lnav_test} -n \
    -c ':config /tuning/archive-manager/min-free-space abc' \
    ${srcdir}/logfile_syslog.0

check_error_output "invalid min-free-space allowed?" <<EOF
✘ error: expecting an integer, found: abc
 --> command-option:1
 | :config /tuning/archive-manager/min-free-space abc
 = help: Synopsis
           :config option [value] - Read or write a configuration option
EOF

run_test ${lnav_test} -n \
    -c ":config /ui/theme baddy" \
    ${test_dir}/logfile_access_log.0

check_error_output "config bad theme" <<EOF
✘ error: invalid value for property “/ui/theme”
 reason: unknown theme -- baddy
 --> command-option:1
EOF

run_test ${lnav_test} -n \
    -I ${test_dir}/bad-config2 \
    ${test_dir}/logfile_access_log.0

check_error_output "config bad theme" <<EOF
✘ error: 'bad' is not a supported configuration \$schema version
 --> {test_dir}/bad-config2/formats/invalid-config/config.bad-schema.json:2
 |     "\$schema": "bad"
 = note: expecting one of the following \$schema values:
           https://lnav.org/schemas/config-v1.schema.json
 = help: Property Synopsis
           /\$schema <schema-uri>
         Description
           The URI that specifies the schema that describes this type of file
         Example
           https://lnav.org/schemas/config-v1.schema.json
⚠ warning: unexpected value for property “/ui”
 --> {test_dir}/bad-config2/formats/invalid-config/config.malformed.json:2
 |     "ui": "theme",
 = help: Available Properties
           \$schema <schema-uri>
           tuning/
           ui/
           global/
✘ error: invalid JSON
 --> {test_dir}/bad-config2/formats/invalid-config/config.malformed.json:3
 | parse error: object key and value must be separated by a colon (':')
 |                "ui": "theme",     "abc",     "def": "" }
 |                      (right here) ------^
 |
⚠ warning: unexpected value for property “/ui”
 --> {test_dir}/bad-config2/formats/invalid-config/config.truncated.json:2
 |     "ui": "theme"
 = help: Available Properties
           \$schema <schema-uri>
           tuning/
           ui/
           global/
✘ error: invalid JSON
 reason: parse error: premature EOF
 --> {test_dir}/bad-config2/formats/invalid-config/config.truncated.json:3
EOF
