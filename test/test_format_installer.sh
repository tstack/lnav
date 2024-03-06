#! /bin/bash

CONFIG_DIR="${top_builddir}/installer-test-home"

mkdir -p "${CONFIG_DIR}"
rm -rf "${CONFIG_DIR}/.lnav/formats"

HOME=${CONFIG_DIR}
unset XDG_CONFIG_HOME
export HOME
export YES_COLOR=1

${lnav_test} -i ${srcdir}/formats/jsontest/format.json

if ! test -f ${CONFIG_DIR}/.lnav/formats/installed/test_log.json; then
    echo "Format not installed correctly?"
    exit 1
fi

run_cap_test ${lnav_test} -i ${srcdir}/formats/jsontest/format.json

echo corrupt > ${CONFIG_DIR}/.lnav/formats/installed/test_log.json

run_cap_test env TEST_COMMENT='overwrite file' ${lnav_test} -i ${srcdir}/formats/jsontest/format.json

if ! test -f ${CONFIG_DIR}/.lnav/formats/installed/test_log.json.bak; then
    echo "Format not backed up correctly?"
    exit 1
fi

run_cap_test ${lnav_test} -i /non-existent/file

if test x"${TEST_GIT_INSTALL}" = x""; then
    # Hitting the git repos frequently is slow/noisy
    exit 0
fi

${lnav_test} -i extra

if ! test -f ${CONFIG_DIR}/.lnav/remote-config/remote-config.json; then
    echo "Remote config not downloaded?"
    exit 1
fi

if ! test -d ${CONFIG_DIR}/.lnav/formats/https___github_com_PaulWay_lnav_formats_git; then
    echo "Third-party repo not downloaded?"
    exit 1
fi
