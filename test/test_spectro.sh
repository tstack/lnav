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

# Spectrogram against a SQL search-table column whose cells are
# humanized text exercises db_spectro_value_source::spectro_value_suffix's
# pull from db_label_source's hm_unit_suffix.  Without the override,
# axis labels and bucket ranges render bare numbers; with it, they
# carry the inferred "B" suffix.
run_cap_test ${lnav_test} -n \
    -c ":create-search-table req_sizes size=(?<size>\S+)" \
    -c ";SELECT log_time, size FROM req_sizes" \
    -c ":spectrogram size" \
    ${test_dir}/logfile_spectro_humanized.0
