#! /bin/bash

export TZ=UTC
export YES_COLOR=1
export LC_ALL=C

lnav_test="${top_builddir}/src/lnav-test"
unset XDG_CONFIG_HOME

run_cap_test ${lnav_test} -n \
    -c ":goto 0" \
    -c ":mark" \
    -c ":spectrogram sc_bytes" \
    ${test_dir}/logfile_access_log.0

run_cap_test ${lnav_test} -n \
    -c ":spectrogram sc_bytes" \
    -c ":mark" \
    -c ":switch-to-view log" \
    ${test_dir}/logfile_access_log.0
