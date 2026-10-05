#! /bin/bash

# Tests for the HTTP server opened by :external-access.  lnav is started in
# the background, found through its discovery file, and stopped before any of
# the checks run, since a failed check exits the script.

# Base64 of "test-key", which is what the server expects in the header.
api_key_header="X-Api-Key: dGVzdC1rZXk="

${lnav_test} -n \
    -c ":external-access 0 test-key" \
    ${test_dir}/logfile_access_log.0 \
    > ext_access_lnav.out 2> ext_access_lnav.err &
lnav_pid=$!

url=""
for lpc in $(seq 1 40); do
    url=$(${lnav_test} -m instances list -o url 2> /dev/null | head -1)
    if test x"${url}" != x""; then
        break
    fi
    if ! kill -0 ${lnav_pid} 2> /dev/null; then
        break
    fi
    sleep 0.25
done

if test x"${url}" = x""; then
    kill ${lnav_pid} 2> /dev/null
    wait ${lnav_pid} 2> /dev/null
    if grep -q "without Rust" ext_access_lnav.err; then
        echo "skipping, lnav was compiled without Rust extensions"
        exit 0
    fi
    echo "lnav did not open an external-access port"
    cat ext_access_lnav.err
    exit 1
fi

curl -s -o ext_access_theme.css \
    -w '%{http_code} %{content_type}\n' \
    -H "${api_key_header}" \
    "${url}/assets/css/theme.css" > ext_access_theme.status

curl -s -o /dev/null \
    -w '%{http_code}\n' \
    "${url}/assets/css/theme.css" > ext_access_no_key.status

curl -s -o ext_access_page.html \
    -H "${api_key_header}" \
    "${url}/apps/lnav/api-test/"

kill ${lnav_pid} 2> /dev/null
wait ${lnav_pid} 2> /dev/null

run_test cat ext_access_theme.status

check_output "theme.css is not served as CSS?" <<EOF
200 text/css; charset=utf8
EOF

run_test grep -c '^\.-lnav_styles_error {' ext_access_theme.css

check_output "theme.css does not have the rules for the theme?" <<EOF
1
EOF

run_test cat ext_access_no_key.status

check_output "theme.css is served without an API key?" <<EOF
401
EOF

run_test grep -c 'href="/assets/css/theme.css"' ext_access_page.html

check_output "rendered pages do not link to theme.css?" <<EOF
1
EOF
