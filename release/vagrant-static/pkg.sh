#! /usr/bin/env bash

OS=$(uname -s)
if test x"${OS}" != x"FreeBSD"; then
    wget -N http://apt.sw.be/redhat/el5/en/x86_64/rpmforge/RPMS/rpmforge-release-0.5.2-2.el5.rf.x86_64.rpm
    sudo rpm --import http://apt.sw.be/RPM-GPG-KEY.dag.txt
    sudo rpm -K rpmforge-release-0.5.2-2.el5.rf.x86_64.rpm
    sudo rpm -U rpmforge-release-0.5.2-2.el5.rf.x86_64.rpm

    sudo yum install -y m4 git gcc44-c++
else
    pkg install -y wget git gcc m4
fi
