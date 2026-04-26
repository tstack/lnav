#!/usr/bin/env bash

export YES_COLOR=1

export HOME="./test-config"
export XDG_CONFIG_HOME="./test-config/.config"
rm -rf ./test-config
mkdir -p $HOME/.config

# config write global var
run_cap_test ${lnav_test} -nN \
    -c ":config /global/foo bar"

# config read global var
run_cap_test ${lnav_test} -nN \
    -c ":config /global/foo"

# Reading a single entry of a pattern_property_handler map must
# return that entry's value, not the first key in the map.  Earlier
# the gen path flat-emitted every (key, value) pair to yajl_gen at
# top level, which captured only the first key as a JSON string.
run_cap_test ${lnav_test} -nN \
    -c ":config /ui/theme-defs/night-owl/vars/red"

run_cap_test ${lnav_test} -nN \
    -c ":config /ui/theme-defs/night-owl/vars/black"

# Distinct global vars must each return their own value (regression
# for the bug where every key returned the first map entry).
run_cap_test ${lnav_test} -nN \
    -c ":config /global/aaa AAA" \
    -c ":config /global/bbb BBB" \
    -c ":config /global/aaa" \
    -c ":config /global/bbb"

# Reading the parent map still returns the full object — the per-key
# fix must not regress the iterate-all path used here.
run_cap_test ${lnav_test} -nN \
    -c ":config /ui/theme-defs/night-owl/vars"

# config bad color
run_cap_test ${lnav_test} -n \
    -c ":config /ui/theme-defs/default/styles/text/color #f" \
    ${test_dir}/logfile_access_log.0

# invalid min-free-space allowed?
rm -rf config-tmp
mkdir config-tmp
run_cap_test env TMPDIR=config-tmp ${lnav_test} -n \
    -c ':config /tuning/archive-manager/min-free-space abc' \
    ${srcdir}/logfile_syslog.0

# config bad theme
run_cap_test ${lnav_test} -n \
    -c ":config /ui/theme baddy" \
    ${test_dir}/logfile_access_log.0

# config bad theme
run_cap_test ${lnav_test} -W -n \
    -I ${test_dir}/bad-config2 \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -nN \
    -c ":reset-config /bad/path"

run_cap_test ${lnav_test} -n -I ${test_dir} \
    hw://seattle/finn
