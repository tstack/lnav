#! /bin/bash

export HOME=${PWD}/remote

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
EOF

SSHD_PATH=$(which sshd)

trap 'kill $(cat remote/sshd.pid)' EXIT

$SSHD_PATH -E ${PWD}/remote/sshd.log -f remote/sshd_config

run_test ${lnav_test} -d /tmp/lnav.err -n \
    nonexistent-host:${test_dir}/logfile_access_log.0

check_error_output "no error for nonexistent-host?" <<EOF
error: unable to open file: nonexistent-host: -- failed to ssh to host: kex_exchange_identification: Connection closed by remote host
EOF

run_test ${lnav_test} -d /tmp/lnav.err -n \
    localhost:${test_dir}/logfile_access_log.0

check_output "could not download remote file?" <<EOF
192.168.202.254 - - [20/Jul/2009:22:59:26 +0000] "GET /vmw/cgi/tramp HTTP/1.0" 200 134 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkboot.gz HTTP/1.0" 404 46210 "-" "gPXE/0.9.7"
192.168.202.254 - - [20/Jul/2009:22:59:29 +0000] "GET /vmw/vSphere/default/vmkernel.gz HTTP/1.0" 200 78929 "-" "gPXE/0.9.7"
EOF
