#! /bin/bash

export YES_COLOR=1
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -n \
    ${top_srcdir}/README.md

run_cap_test ${lnav_test} -n \
    ${top_srcdir}/src/log_level.cc
