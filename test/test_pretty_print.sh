#! /bin/bash

run_test ${lnav_test} -n \
    -c ":switch-to-view pretty" \
    ${test_dir}/logfile_cxx.0
check_output "pretty-printer is not working for C++ dumps" <<EOF
Mar 24 15:17:38.999  000000000264F I shmem.res 262144 262144 1 chassis_msg_svc/osenv::req_blocking<osenv::req_lambda<tcp_messaging_impl::register_app(svc::messaging_port,
    defs::atom*,
    defs::borrowed<svc::messaging_session>,
    defs::owned<svc::connection_eviction_strategy>&&,
    svc::messaging::connection_type,
    svc::messaging::app_param
)::{lambda()#1}>, osenv::aloc_dynamic_named<tcp_messaging_impl::register_app(svc::messaging_port,
    defs::atom*,
    defs::borrowed<svc::messaging_session>,
    defs::owned<svc::connection_eviction_strategy>&&,
    svc::messaginconnection_type,
    svc::messaging::app_param
)::{lambda()#1}, osenv::temporal, tcp_messaging_impl::register_app(svc::messaging_port,
    defs::atom*,
    defs::borrowed<svc::messaging_session>,
    des::owned<svc::connection_eviction_strategy>&&,
    svc::messaging::connection_type,
    svc::messaging::app_param
)::{lambda()#1}>, osenv::req>->fiber stacks
EOF

echo '2015-04-18T13:16:30.003 8.8.8.8 <foo>8.8.8.8</foo>9 8.8.8.8<1054 198.51.100.1546 544.9.8.7 98.542.241.99 19143.2.5.6' | \
    run_test ${lnav_test} -n -c ":switch-to-view pretty"

check_output "pretty print not able to properly grok ipv4?" <<EOF
2015-04-18T13:16:30.003 8.8.8.8 (google-public-dns-a.google.com)
<foo>8.8.8.8 (google-public-dns-a.google.com)</foo>
9 8.8.8.8 (google-public-dns-a.google.com)<1054 198.51.100.1546 544.9.8.7 98.542.241.99 19143.2.5.6
EOF

cat <<EOF
2015-04-18T13:16:30.003 {"wrapper": {"msg": r"Hello,\nWorld!\n"}}
EOF | run_test ${lnav_test} -n -c ":switch-to-view pretty"

check_output "pretty print is not interpreting quoted strings correctly?" <<EOF
2015-04-18T13:16:30.003 {
    "wrapper": {"msg": r""
        Hello,
        World!
""}}
EOF

cat <<EOF
{"wrapper": [{"message":"\nselect Id from Account where id = $sfid\n                                 ^\nERROR at Row:1:Column:34\nline 1:34 no viable alternative at character '$'"}]}
EOF | run_test ${lnav_test} -n -c ":switch-to-view pretty"

check_output "pretty print is not including leading white space?" <<EOF
{
    "wrapper": [
            {"message":""
            select Id from Account where id = \$sfid
                                             ^
            ERROR at Row:1:Column:34
            line 1:34 no viable alternative at character '\$'
""}]}
EOF
