#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_cxx.0

# check for ipv4 strings
run_cap_test ${lnav_test} -n -c ":switch-to-view pretty" <<EOF
2015-04-18T13:16:30.003 8.8.8.8 <foo>8.8.8.8</foo>9 8.8.8.8<1054 198.51.100.1546 544.9.8.7 98.542.241.99 19143.2.5.6
EOF

cat > test_pretty_in.1 <<EOF
2015-04-18T13:16:30.003 {"wrapper": {"msg": r"Hello,\nWorld!\n"}}
EOF

# pretty print can interpret quoted strings correctly
run_cap_test ${lnav_test} -n -c ":switch-to-view pretty" -d /tmp/lnav.err test_pretty_in.1

cat > test_pretty_in.2 <<EOF
{"wrapper": [{"message":"\nselect Id from Account where id = \$sfid\n                                 ^\nERROR at Row:1:Column:34\nline 1:34 no viable alternative at character '$'"}]}
EOF

# pretty print includes leading white space
run_cap_test ${lnav_test} -n -c ":switch-to-view pretty" test_pretty_in.2

cat > test_pretty_in.3 <<EOF
Hello\\nWorld\\n
EOF

run_cap_test ${lnav_test} -d /tmp/lnav.err -n -c ":switch-to-view pretty" test_pretty_in.3

run_cap_test ${lnav_test} -d /tmp/lnav.err -n \
    -I ${test_dir} \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_xml_msg.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_ansi.0

run_cap_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/textfile_ansi.0

run_cap_test ${lnav_test} -n \
    -c ':switch-to-view pretty' \
    -c ':goto 2' \
    -c ';SELECT * FROM lnav_views' \
    -c ':write-json-to -' \
    ${test_dir}/textfile_json_one_line.0
