#! /bin/bash

export YES_COLOR=1

if test x"${TEST_REGEX101}" = x""; then
    # Hitting the git repos frequently is slow/noisy
    exit 0
fi

rm -rf regex101-home
mkdir -p regex101-home
export HOME=regex101-home

run_cap_test ${lnav_test} -m format syslog_log regex std

run_cap_test ${lnav_test} -m format syslog_log regex std regex101

run_cap_test ${lnav_test} -m format syslog_log regex std regex101 pull

run_cap_test ${lnav_test} -m format syslog_log regex std regex101 delete

run_cap_test env TEST_COMMENT="before import" ${lnav_test} -m regex101 list

run_cap_test ${lnav_test} -m regex101 import

run_cap_test ${lnav_test} -m regex101 import abc def-jkl

run_cap_test ${lnav_test} -m regex101 import https://regex101.com/r/badregex123/1 unit_test_log

# bad regex flavor
run_cap_test ${lnav_test} -m regex101 import https://regex101.com/r/cvCJNP/1 unit_test_log

run_cap_test ${lnav_test} -m regex101 import https://regex101.com/r/zpEnjV/2 unit_test_log

# a second import should fail since the format file exists now
run_cap_test ${lnav_test} -m regex101 import https://regex101.com/r/zpEnjV/1 unit_test_log

run_cap_test cat regex101-home/.lnav/formats/installed/unit_test_log.json

run_cap_test env TEST_COMMENT="after import" ${lnav_test} -m regex101 list

run_cap_test ${lnav_test} -m format non-existent regex std regex101 pull

run_cap_test ${lnav_test} -m format bro regex std regex101 pull

run_cap_test ${lnav_test} -m format unit_test_log regex non-existent regex101 pull

run_cap_test ${lnav_test} -m format unit_test_log regex s regex101 pull

run_cap_test ${lnav_test} -m format unit_test_log regex std regex101

run_cap_test ${lnav_test} -m format unit_test_log regex std regex101 pull

cat > regex101-home/.lnav/formats/installed/unit_test_log.regex101-zpEnjV.json <<EOF
{
    "unit_test_log": {
        "regex": {
            "std": {
                "pattern": ""
            }
        }
    }
}
EOF

run_cap_test env TEST_COMMENT="pull after change" \
    ${lnav_test} -m format unit_test_log regex std regex101 pull

run_cap_test ${lnav_test} -m format unit_test_log sources

run_cap_test cat regex101-home/.lnav/formats/installed/unit_test_log.regex101-zpEnjV.json

run_cap_test ${lnav_test} -m regex101 import https://regex101.com/r/hGiqBL/2 unit_test_log alt

run_cap_test cat regex101-home/.lnav/formats/installed/unit_test_log.regex101-hGiqBL.json

run_cap_test ${lnav_test} -m format unit_test_log regex std regex101 delete

rm regex101-home/.lnav/formats/installed/unit_test_log.regex101-zpEnjV.json

run_cap_test env TEST_COMMENT="delete after patch removed" \
    ${lnav_test} -m format unit_test_log regex std regex101 delete
