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
