#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"

run_test ./scripty -n -e ${srcdir}/tui-captures/tui_help.0 -- \
    ${lnav_test} -H < /dev/null

on_error_fail_with "help screen does not work?"
