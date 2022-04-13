#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"


run_test ${lnav_test} -C \
    -I ${test_dir}/bad-config-json

check_error_output "invalid format not detected?" <<EOF
✘ error: invalid JSON
 --> {test_dir}/bad-config-json/formats/invalid-json/format.json:4
 | parse error: object key and value must be separated by a colon (':')
 |           ar_log": {         "abc"     } }
 |                      (right here) ------^
 |
✘ error: “abc(” is not a valid regular expression for property “/invalid_key_log/level-pointer”
 reason: missing )
 --> {test_dir}/bad-config-json/formats/invalid-key/format.json:4
 |         "level-pointer": "abc(",
 --> /invalid_key_log/level-pointer
 | abc(
 |     ^ missing )
 = help: Property Synopsis
           /invalid_key_log/level-pointer
         Description
           A regular-expression that matches the JSON-pointer of the level property
✘ error: “def[ghi” is not a valid regular expression for property “/invalid_key_log/file-pattern”
 reason: missing terminating ] for character class
 --> {test_dir}/bad-config-json/formats/invalid-key/format.json:5
 |         "file-pattern": "def[ghi",
 --> /invalid_key_log/file-pattern
 | def[ghi
 |        ^ missing terminating ] for character class
 = help: Property Synopsis
           /invalid_key_log/file-pattern
         Description
           A regular expression that restricts this format to log files with a matching name
⚠ warning: unexpected value for property “/invalid_key_log/value/test/identifiers”
 --> {test_dir}/bad-config-json/formats/invalid-key/format.json:14
 |                 "identifiers": true
 = help: Available Properties
           kind <data-type>
           collate <function>
           unit/
           identifier <bool>
           foreign-key <bool>
           hidden <bool>
           action-list <string>
           rewriter <command>
           description <string>
✘ error: “-1.2” is not a valid value for “/invalid_key_log/timestamp-divisor”
 reason: value cannot be less than or equal to zero
 --> {test_dir}/bad-config-json/formats/invalid-key/format.json:25
 |         "timestamp-divisor": -1.2
 = help: Property Synopsis
           /invalid_key_log/timestamp-divisor <number>
         Description
           The value to divide a numeric timestamp by in a JSON log.
✘ error: “foobar_log” is not a valid log format
 reason: no regexes specified
 --> {test_dir}/bad-config-json/formats/invalid-json/format.json:3
✘ error: “foobar_log” is not a valid log format
 reason: log message samples must be included in a format definition
 --> {test_dir}/bad-config-json/formats/invalid-json/format.json:3
✘ error: “invalid_key_log” is not a valid log format
 reason: structured logs cannot have regexes
 --> {test_dir}/bad-config-json/formats/invalid-key/format.json:4
✘ error: invalid line format element “/invalid_key_log/line-format/0/field”
 reason: “non-existent” is not a defined value
 --> {test_dir}/bad-config-json/formats/invalid-key/format.json:22
EOF

run_test env LC_ALL=C ${lnav_test} -C \
    -I ${test_dir}/bad-config

check_error_output "invalid format not detected?" <<EOF
✘ error: “abc(def” is not a valid regular expression for property “/invalid_props_log/search-table/bad_table_regex/pattern”
 reason: missing )
 --> {test_dir}/bad-config/formats/invalid-properties/format.json:24
 |                 "pattern": "abc(def"
 --> /invalid_props_log/search-table/bad_table_regex/pattern
 | abc(def
 |        ^ missing )
 = help: Property Synopsis
           /invalid_props_log/search-table/bad_table_regex/pattern <regex>
         Description
           The regular expression for this search table.
✘ error: “^(?<timestamp>\d+: (?<body>.*)\$” is not a valid regular expression for property “/bad_regex_log/regex/std/pattern”
 reason: missing )
 --> {test_dir}/bad-config/formats/invalid-regex/format.json:6
 |                 "pattern": "^(?<timestamp>\\\\d+: (?<body>.*)$"
 --> /bad_regex_log/regex/std/pattern
 | ^(?<timestamp>\d+: (?<body>.*)$
 |                                ^ missing )
 = help: Property Synopsis
           /bad_regex_log/regex/std/pattern <message-regex>
         Description
           The regular expression to match a log message and capture fields.
✘ error: “(foo” is not a valid regular expression for property “/bad_regex_log/level/error”
 reason: missing )
 --> {test_dir}/bad-config/formats/invalid-regex/format.json:10
 |             "error" : "(foo"
 --> /bad_regex_log/level/error
 | (foo
 |     ^ missing )
 = help: Property Synopsis
           /bad_regex_log/level/error <pattern|integer>
         Description
           The regular expression used to match the log text for this level.  For JSON logs with numeric levels, this should be the number for the corresponding level.
✘ error: “abc(” is not a valid regular expression for property “/bad_regex_log/highlights/foobar/pattern”
 reason: missing )
 --> {test_dir}/bad-config/formats/invalid-regex/format.json:22
 |                 "pattern": "abc("
 --> /bad_regex_log/highlights/foobar/pattern
 | abc(
 |     ^ missing )
 = help: Property Synopsis
           /bad_regex_log/highlights/foobar/pattern <regex>
         Description
           A regular expression to highlight in logs of this format.
✘ error: “foo” is not a valid value for option “/bad_sample_log/value/pid/kind”
 --> {test_dir}/bad-config/formats/invalid-sample/format.json:24
 |                 "kind": "foo"
 = help: Property Synopsis
           /bad_sample_log/value/pid/kind <data-type>
         Description
           The type of data in the field
         Allowed Values
           string, integer, float, boolean, json, struct, quoted, xml
✘ error: 'bad' is not a supported log format \$schema version
 --> {test_dir}/bad-config/formats/invalid-schema/format.json:2
 |     "\$schema": "bad"
 = note: expecting one of the following \$schema values:
           https://lnav.org/schemas/format-v1.schema.json
 = help: Property Synopsis
           /\$schema The URI of the schema for this file
         Description
           Specifies the type of this file
✘ error: invalid sample log message “abc: foo”
 reason: unrecognized timestamp -- abc
 --> {test_dir}/bad-config/formats/invalid-sample/format.json:30
 = note: the following formats were tried:
           abc
           ^ “%i” matched up to here
✘ error: invalid sample log message “1428634687123| debug hello”
 reason: “debug” does not match the expected level of “info”
 --> {test_dir}/bad-config/formats/invalid-sample/format.json:33
✘ error: invalid sample log message “1428634687123; foo bar”
 reason: sample does not match any patterns
 --> {test_dir}/bad-config/formats/invalid-sample/format.json:37
 = note: the following shows how each pattern matched this sample:
           “1428634687123; foo bar”
                         ^ bad-time matched up to here
                              ^ semi matched up to here
                         ^ std matched up to here
                         ^ with-level matched up to here
 = help: bad-time   = ^(?<timestamp>\w+): (?<body>\w+)$
         semi       = ^(?<timestamp>\d+); (?<body>\w+)$
         std        = ^(?<timestamp>\d+): (?<pid>\w+) (?<body>.*)$
         with-level = ^(?<timestamp>\d+)\| (?<level>\w+) (?<body>\w+)$
✘ error: invalid value for property “/invalid_props_log/timestamp-field”
 reason: “ts” was not found in the pattern at /invalid_props_log/regex/std
 --> {test_dir}/bad-config/formats/invalid-properties/format.json:4
 = note: the following captures are available:
           body, pid, timestamp
✘ error: “not a color” is not a valid color value for property “/invalid_props_log/highlights/hl1/color”
 reason: Unknown color: 'not a color'.  See https://jonasjacek.github.io/colors/ for a list of supported color names
 --> {test_dir}/bad-config/formats/invalid-properties/format.json:18
✘ error: “also not a color” is not a valid color value for property “/invalid_props_log/highlights/hl1/background-color”
 reason: Unknown color: 'also not a color'.  See https://jonasjacek.github.io/colors/ for a list of supported color names
 --> {test_dir}/bad-config/formats/invalid-properties/format.json:19
✘ error: “no_regexes_log” is not a valid log format
 reason: no regexes specified
 --> {test_dir}/bad-config/formats/no-regexes/format.json:4
✘ error: “no_regexes_log” is not a valid log format
 reason: log message samples must be included in a format definition
 --> {test_dir}/bad-config/formats/no-regexes/format.json:4
✘ error: “no_sample_log” is not a valid log format
 reason: log message samples must be included in a format definition
 --> {test_dir}/bad-config/formats/no-samples/format.json:4
✘ error: failed to compile SQL statement
 reason: near "TALE": syntax error
 --> {test_dir}/bad-config/formats/invalid-sql/init.sql:4
 | -- comment test
 | CREATE TALE
✘ error: failed to execute SQL statement
 reason: missing )
 --> {test_dir}/bad-config/formats/invalid-sql/init2.sql
 | SELECT regexp_match('abc(', '123')
 | FROM sqlite_master;
EOF

run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ";select * from leveltest_log" \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_leveltest.0

check_output "levels are not correct?" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters
0,<NULL>,2016-06-30 12:00:01.000,0,trace,0,<NULL>,<NULL>,<NULL>
1,<NULL>,2016-06-30 12:00:02.000,1000,debug,0,<NULL>,<NULL>,<NULL>
2,<NULL>,2016-06-30 12:00:03.000,1000,debug2,0,<NULL>,<NULL>,<NULL>
3,<NULL>,2016-06-30 12:00:04.000,1000,debug3,0,<NULL>,<NULL>,<NULL>
4,<NULL>,2016-06-30 12:00:05.000,1000,info,0,<NULL>,<NULL>,<NULL>
5,<NULL>,2016-06-30 12:00:06.000,1000,warning,0,<NULL>,<NULL>,<NULL>
6,<NULL>,2016-06-30 12:00:07.000,1000,fatal,0,<NULL>,<NULL>,<NULL>
7,<NULL>,2016-06-30 12:00:08.000,1000,info,0,<NULL>,<NULL>,<NULL>
EOF
