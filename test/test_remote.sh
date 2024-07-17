#! /bin/bash

echo "Hello, World!" > not:a:remote:file

run_test ${lnav_test} -d /tmp/lnav.err -n \
    not:a:remote:file

check_output "a file with colons cannot be read?" <<EOF
Hello, World!
EOF

run_test ${lnav_test} -d /tmp/lnav.err -Nn \
    -c ':open not:a:remote:file'

check_output "a file with colons cannot be read?" <<EOF
Hello, World!
EOF

mkdir not:a:remote:dir
echo "Hello, World!" > not:a:remote:dir/file

run_test ${lnav_test} -d /tmp/lnav.err -n \
    not:a:remote:dir

check_output "a file in a dir with colons cannot be read?" <<EOF
Hello, World!
EOF

run_test ${lnav_test} -d /tmp/lnav.err -n \
    not:a:remote:dir/f*

check_output "a wildcard in a dir with colons cannot be read?" <<EOF
Hello, World!
EOF
if [ -d /home/runner ]; then
chmod 755 /home/runner
ls -la /home/runner
fi
export HOME=${PWD}/remote
unset XDG_CONFIG_HOME

rm -rf remote-tmp
mkdir -p remote-tmp
export TMPDIR=remote-tmp

cat > remote/sshd_config <<EOF
Port 2222
UsePam no
AuthorizedKeysFile ${PWD}/remote/authorized_keys
HostKey ${PWD}/remote/ssh_host_rsa_key
HostKey ${PWD}/remote/ssh_host_dsa_key
ChallengeResponseAuthentication no
PidFile ${PWD}/remote/sshd.pid
EOF

cat > remote/ssh_config <<EOF
Host *
Port 2222
IdentityFile ${PWD}/remote/id_rsa
StrictHostKeyChecking no
EOF

SSHD_PATH=$(which sshd)
echo "ssh path: ${SSHD_PATH}"

trap 'kill $(cat remote/sshd.pid)' EXIT

$SSHD_PATH -E ${PWD}/remote/sshd.log -f remote/sshd_config

${lnav_test} -d /tmp/lnav.err -nN \
    -c ":config /tuning/remote/ssh/options/F ${PWD}/remote/ssh_config"

run_test ${lnav_test} -d /tmp/lnav.err -n \
    nonexistent-host:${test_dir}/logfile_access_log.0

sed -e "s|ssh:.*|...|g" `test_err_filename` | head -1 \
    > test_remote.err

mv test_remote.err `test_err_filename`
check_error_output "no error for nonexistent-host?" <<EOF
error: unable to open file: nonexistent-host: -- failed to ssh to host: ...
EOF

run_test ${lnav_test} -d /tmp/lnav.err -n \
    nonexistent-host:${test_dir}/logfile_access_log.*

sed -e "s|ssh:.*|...|g" `test_err_filename` | head -1 \
    > test_remote.err

mv test_remote.err `test_err_filename`
check_error_output "no error for nonexistent-host?" <<EOF
error: unable to open file: nonexistent-host: -- failed to ssh to host: ...
EOF

run_test ${lnav_test} -d /tmp/lnav.err -n \
    localhost:nonexistent-file

cat remote/sshd.log
check_error_output "no error for nonexistent-file?" <<EOF
error: unable to open file: localhost:nonexistent-file -- unable to lstat -- ENOENT[2]
EOF

run_test ${lnav_test} -d /tmp/lnav.err -n \
    localhost:${test_dir}/logfile_access_log.0

check_output "could not download remote file?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF

run_test ${lnav_test} -d /tmp/lnav.err -n \
    "localhost:${test_dir}/logfile_access_log.*"

check_output "could not download remote file?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
10.112.81.15 - - [15/Feb/2013:06:00:31 +0000] "-" 400 0 "-" "-"
EOF

run_test ${lnav_test} -d /tmp/lnav.err -n \
    "localhost:${test_dir}/remote-log-dir"

check_output "could not download remote file?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
10.112.81.15 - - [15/Feb/2013:06:00:31 +0000] "-" 400 0 "-" "-"
EOF
