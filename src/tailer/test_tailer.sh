#!/bin/bash

run_test ./drive_tailer preview nonexistent-file

check_error_output "preview of nonexistent-file failed?" <<EOF
preview error: nonexistent-file -- error: cannot open nonexistent-file -- No such file or directory
tailer stderr:
info: load preview request -- 1234
info: exiting...
EOF

run_test ./drive_tailer preview ${test_dir}/logfile_access_log.0

check_output "preview of file failed?" <<EOF
preview of file: {test_dir}/logfile_access_log.0
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"

all done!
tailer stderr:
info: load preview request -- 1234
info: exiting...
EOF

run_cap_test ./drive_tailer preview "${test_dir}/remote-log-dir/*"

run_test ./drive_tailer possible "${test_dir}/logfile_access_log.*"

check_output "possible path list failed?" <<EOF
possible path: {test_dir}/logfile_access_log.0
possible path: {test_dir}/logfile_access_log.1
all done!
tailer stderr:
complete path: {test_dir}/logfile_access_log.*
complete glob path: {test_dir}/logfile_access_log.*
info: exiting...
EOF

ln -sf bar foo

run_test ./drive_tailer open foo

check_output "open link not working?" <<EOF
link value: foo -> bar
all done!
tailer stderr:
info: monitoring path: foo
info: exiting...
EOF
