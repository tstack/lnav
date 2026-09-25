#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -nN -c ";SELECT yaml_to_json('[abc')"

run_cap_test ./drive_sql "SELECT yaml_to_json('abc: def')"

# the YAML 1.2 core schema spellings of null, booleans, and integers
run_cap_test ./drive_sql "SELECT yaml_to_json('a:
b: ~
c: NULL
d: False
e: TRUE
f: 0x1F
g: 0o17
h: \"~\"
i: |-
  null')"

# aliases are expanded
run_cap_test ./drive_sql "SELECT yaml_to_json('a: &x 1
b: *x
c: &y [1, 2]
d: *y')"

# each document in a stream becomes an array element
run_cap_test ./drive_sql "SELECT yaml_to_json('---
a: 1
---
b: 2
')"

# an error from the emitter has no snippet of the input
run_cap_test ${lnav_test} -nN -c ";SELECT yaml_to_json('a: !!str 1')"
