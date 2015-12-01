#! /bin/bash

run_test ./drive_json_op get "" <<EOF
3
EOF

check_output "cannot read root number value" <<EOF
3
EOF

run_test ./drive_json_op get "" <<EOF
null
EOF

check_output "cannot read root null value" <<EOF
null
EOF

run_test ./drive_json_op get "" <<EOF
true
EOF

check_output "cannot read root bool value" <<EOF
true
EOF

run_test ./drive_json_op get "" <<EOF
"str"
EOF

check_output "cannot read root string value" <<EOF
"str"
EOF

run_test ./drive_json_op get "" <<EOF
{ "val" : 3, "other" : 2 }
EOF

check_output "cannot read root map value" <<EOF
{
    "val": 3,
    "other": 2
}
EOF

run_test ./drive_json_op get /val <<EOF
{ "val" : 3 }
EOF

check_output "cannot read top-level value" <<EOF
3
EOF

run_test ./drive_json_op get /val <<EOF
{ "other" : { "val" : 5 }, "val" : 3 }
EOF

check_output "read wrong value" <<EOF
3
EOF

run_test ./drive_json_op get /other <<EOF
{ "other" : { "val" : 5 }, "val" : 3 }
EOF

check_output "cannot read map" <<EOF
{
    "val": 5
}
EOF

run_test ./drive_json_op get /other/val <<EOF
{ "other" : { "val" : 5 }, "val" : 3 }
EOF

check_output "cannot read nested map" <<EOF
5
EOF


run_test ./drive_json_op get "" <<EOF
[0, 1]
EOF

check_output "cannot read root array value" <<EOF
[
    0,
    1
]
EOF

run_test ./drive_json_op get "/6" <<EOF
[null, true, 1, "str", {"sub":[10, 11]}, [21, [33, 34], 66], 2]
EOF

check_output "cannot read array value" <<EOF
2
EOF

run_test ./drive_json_op get "/ID" <<EOF
{"ID":"P1","ProcessID":"P1","Name":"VxWorks","CanSuspend":true,"CanResume":1,"IsContainer":true,"WordSize":4,"CanTerminate":true,"CanDetach":true,"RCGroup":"P1","SymbolsGroup":"P1","CPUGroup":"P1","DiagnosticTestProcess":true}
EOF
