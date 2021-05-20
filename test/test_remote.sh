#! /bin/bash

cat > remote/sshd_config <<EOF
Port 2222
UsePam no
AuthorizedKeysFile  .ssh/authorized_keys
HostKey ${PWD}/remote/ssh_host_rsa_key
HostKey ${PWD}/remote/ssh_host_dsa_key
ChallengeResponseAuthentication no
PidFile ${PWD}/remote/sshd.pid
EOF

SSHD_PATH=$(which sshd)

# trap 'kill $(cat remote/sshd.pid)' EXIT

$SSHD_PATH -E ${PWD}/remote/sshd.log -f remote/sshd_config

${lnav_test} -nN \
    -c ":config /tuning/remote/ssh/options/p 2222" \
    -c ":config /tuning/remote/ssh/flags vv"

run_test ${lnav_test} -d /tmp/lnav.err -n localhost:testdir

check_output "config write global var" <<EOF
EOF
