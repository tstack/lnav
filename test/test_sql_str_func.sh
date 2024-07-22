#! /bin/bash

export YES_COLOR=1

run_cap_test ./drive_sql "select length(gzip(1))"

run_cap_test ./drive_sql "select gunzip(gzip(1))"

run_cap_test ./drive_sql "select humanize_file_size()"

run_cap_test ./drive_sql "select humanize_file_size('abc')"

run_cap_test ./drive_sql "select humanize_file_size(1, 2)"

run_cap_test ./drive_sql "select humanize_file_size(10 * 1000 * 1000)"

run_cap_test ./drive_sql "select startswith('.foo', '.')"

run_cap_test ./drive_sql "select startswith('foo', '.')"

run_cap_test ./drive_sql "select endswith('foo', '.')"

run_cap_test ./drive_sql "select endswith('foo.', '.')"

run_cap_test ./drive_sql "select endswith('foo.txt', '.txt')"

run_cap_test ./drive_sql "select endswith('a', '.txt')"

run_cap_test ./drive_sql "select regexp('abcd', 'abcd')"

run_cap_test ./drive_sql "select regexp('bc', 'abcd')"

run_cap_test ./drive_sql "select regexp('[e-z]+', 'abcd')"

run_cap_test ./drive_sql "select regexp('[e-z]+', 'ea')"

run_cap_test ./drive_sql "select regexp_replace('test 1 2 3', '\\d+', 'N')"

run_cap_test env TEST_COMMENT=regexp_replace_with_bs1 ./drive_sql <<'EOF'
select regexp_replace('test 1 2 3', '\s+', '{\0}') as repl
EOF

run_cap_test env TEST_COMMENT=regexp_replace_with_bs2 ./drive_sql <<'EOF'
select regexp_replace('test 1 2 3', '\w*', '{\0}') as repl
EOF

run_cap_test ./drive_sql "select regexp_replace('123 abc', '(\w*)', '<\3>') as repl"

run_cap_test env TEST_COMMENT=regexp_replace_with_bs3 ./drive_sql <<'EOF'
select regexp_replace('123 abc', '(\w*)', '<\\>') as repl
EOF

run_cap_test ./drive_sql "select regexp_replace('abc: def', '(\w*):\s*(.*)', '\1=\2') as repl"

run_cap_test ./drive_sql "select regexp_match('abc', 'abc')"

run_cap_test ./drive_sql "select regexp_match(null, 'abc')"

run_cap_test ./drive_sql "select regexp_match('abc', null) as result"

run_cap_test ./drive_sql "select typeof(result), result from (select regexp_match('(\d*)abc', 'abc') as result)"

run_cap_test ./drive_sql "select typeof(result), result from (select regexp_match('(\d*)abc(\d*)', 'abc') as result)"

run_cap_test ./drive_sql "select typeof(result), result from (select regexp_match('(\d+)', '123') as result)"

run_cap_test ./drive_sql "select typeof(result), result from (select regexp_match('a(\d+\.\d+)a', 'a123.456a') as result)"

run_cap_test ./drive_sql "select regexp_match('foo=(?<foo>\w+); (\w+)', 'foo=abc; 123') as result"

run_cap_test ./drive_sql "select regexp_match('foo=(?<foo>\w+); (\w+\.\w+)', 'foo=abc; 123.456') as result"

run_cap_test ${lnav_test} -nN \
   -c ";SELECT regexp_match('^(\w+)=([^;]+);', 'abc=def;ghi=jkl;')"

run_cap_test ./drive_sql "select extract('foo=1') as result"

run_cap_test ./drive_sql "select extract('foo=1; bar=2') as result"

run_cap_test ./drive_sql "select extract(null) as result"

run_cap_test ./drive_sql "select extract(1) as result"

run_cap_test ./drive_sql "select logfmt2json('foo=1 bar=2 baz=2e1 msg=hello') as result"

run_cap_test ./drive_sql "SELECT substr('#foo', range_start) AS value FROM regexp_capture('#foo', '(\w+)') WHERE capture_index = 1"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '\w+ (\w+)')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '\w+ \w+')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '\w+ (?<word>\w+)')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '(bar)|\w+ (?<word>\w+)')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture()"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('foo bar')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('foo bar', '(')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('1 2 3 45', '(\d+)')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture('foo foo', '^foo')"

run_cap_test ./drive_sql "SELECT * FROM regexp_capture_into_json('foo=1 bar=2; foo=3 bar=4', 'foo=(\d+) bar=(\d+)')"

run_cap_test ./drive_sql "SELECT encode('foo', 'bar')"

run_cap_test ./drive_sql "SELECT encode('foo', null)"

run_cap_test ./drive_sql "SELECT encode(null, 'base64')"

run_cap_test ./drive_sql "SELECT encode('hi' || char(10), 'hex')"

run_cap_test ./drive_sql "SELECT gunzip(decode(encode(gzip('Hello, World!'), 'base64'), 'base64'))"

#run_cap_test env TEST_COMMENT=invalid_url ./drive_sql <<'EOF'
#SELECT parse_url('https://bad@[fe::')
#EOF

run_cap_test env TEST_COMMENT=unsupported_url ./drive_sql <<'EOF'
SELECT parse_url('https://example.com:100000')
EOF

run_cap_test env TEST_COMMENT=parse_url1 ./drive_sql <<'EOF'
SELECT parse_url('https://example.com')
EOF

run_cap_test env TEST_COMMENT=parse_url2 ./drive_sql <<'EOF'
SELECT parse_url('https://example.com/')
EOF

run_cap_test env TEST_COMMENT=parse_url3 ./drive_sql <<'EOF'
SELECT parse_url('https://example.com/search?flag')
EOF

run_cap_test env TEST_COMMENT=parse_url4 ./drive_sql <<'EOF'
SELECT parse_url('https://example.com/search?flag&flag2')
EOF

run_cap_test env TEST_COMMENT=parse_url5 ./drive_sql <<'EOF'
SELECT parse_url('https://example.com/search?flag&flag2&=def')
EOF

run_cap_test env TEST_COMMENT=parse_url6 ./drive_sql <<'EOF'
SELECT parse_url('https://example.com/sea%26rch?flag&flag2&=def#frag1%20space')
EOF

run_cap_test env TEST_COMMENT=parse_url7 ./drive_sql <<'EOF'
SELECT parse_url('https://example.com/sea%26rch?flag&flag2&=def&flag3=abc+def#frag1%20space')
EOF


run_cap_test env TEST_COMMENT=unparse_url3 ./drive_sql <<'EOF'
SELECT unparse_url(parse_url('https://example.com/search?flag'))
EOF

run_cap_test env TEST_COMMENT=unparse_url4 ./drive_sql <<'EOF'
SELECT unparse_url(parse_url('https://example.com/search?flag&flag2'))
EOF

run_cap_test env TEST_COMMENT=unparse_url5 ./drive_sql <<'EOF'
SELECT unparse_url(parse_url('https://example.com/search?flag&flag2&=def'))
EOF

run_cap_test env TEST_COMMENT=unparse_url6 ./drive_sql <<'EOF'
SELECT unparse_url(parse_url('https://example.com/search?flag&flag2&=def#frag1%20space'))
EOF

run_cap_test env TEST_COMMENT=unparse_url7 ./drive_sql <<'EOF'
SELECT unparse_url(NULL)
EOF

run_cap_test env TEST_COMMENT=unparse_url8 ./drive_sql <<'EOF'
SELECT unparse_url(123)
EOF

run_cap_test env TEST_COMMENT=unparse_url9 ./drive_sql <<'EOF'
SELECT unparse_url('[1, 2, 3]')
EOF

run_cap_test env TEST_COMMENT=unparse_url10 ./drive_sql <<'EOF'
SELECT unparse_url(json_object('unknown', 'abc'))
EOF

run_cap_test env TEST_COMMENT=unparse_url11 ./drive_sql <<'EOF'
SELECT unparse_url('{}')
EOF

run_cap_test ./drive_sql "SELECT pretty_print('{a: 1, b:2}')"

run_cap_test ${lnav_test} -n \
    -c ';SELECT log_body, extract(log_body) from vmw_log' \
    -c ':write-json-to -' \
    ${test_dir}/logfile_vmw_log.0

run_cap_test ${lnav_test} -n \
    -c ';SELECT anonymize(bro_id_resp_h) FROM bro_http_log' \
    ${test_dir}/logfile_bro_http.log.0

run_cap_test ${lnav_test} -nN \
    -c ";SELECT humanize_id('foo'), humanize_id('bar')"
