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

# A format that parses but whose sample matches nothing must not install.
run_cap_test env TEST_COMMENT='invalid sample' ${lnav_test} -i \
    ${test_dir}/bad-config/formats/invalid-sample/format.json

if test -f ${CONFIG_DIR}/.lnav/formats/installed/invalid_sample_log.json; then
    echo "Format with an invalid sample was installed?"
    exit 1
fi

# A format with no timestamp capture must not install.
run_cap_test env TEST_COMMENT='no timestamp capture' ${lnav_test} -i \
    ${test_dir}/bad-config/formats/invalid-no-tscap/format.json

# A SQL script that does not run must not install.
run_cap_test env TEST_COMMENT='bad sql' ${lnav_test} -i \
    ${test_dir}/bad-config/formats/invalid-sql/init.sql

if test -f ${CONFIG_DIR}/.lnav/formats/installed/init.sql; then
    echo "SQL with a syntax error was installed?"
    exit 1
fi

# A SQL script that refers to a log table has to keep working.
run_cap_test env TEST_COMMENT='sql with log table' ${lnav_test} -i \
    ${test_dir}/formats/sqldir/init.sql

if ! test -f ${CONFIG_DIR}/.lnav/formats/installed/init.sql; then
    echo "SQL that refers to a log table was not installed?"
    exit 1
fi

# Installing it a second time has to keep working.
run_cap_test env TEST_COMMENT='sql reinstall' ${lnav_test} -i \
    ${test_dir}/formats/sqldir/init.sql

# A config file with a bad property must not install.
run_cap_test env TEST_COMMENT='invalid theme' ${lnav_test} -i \
    ${test_dir}/bad-config2/configs/invalid-theme/config.json

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
