#! /bin/bash

FAKE_ROOT=/home/vagrant/fake.root

rm -rf ~/extract
mkdir -p ~/fake.root ~/packages ~/extract ~/github

export PATH=${FAKE_ROOT}/bin:${PATH}

wget -N http://apt.sw.be/redhat/el5/en/x86_64/rpmforge/RPMS/rpmforge-release-0.5.2-2.el5.rf.x86_64.rpm
sudo rpm --import http://apt.sw.be/RPM-GPG-KEY.dag.txt
sudo rpm -K rpmforge-release-0.5.2-2.el5.rf.x86_64.rpm
sudo rpm -U rpmforge-release-0.5.2-2.el5.rf.x86_64.rpm

sudo yum install -y m4 git gcc44-c++

cd ~/github

PACKAGE_URLS="\
    http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz \
    http://ftp.gnu.org/gnu/automake/automake-1.14.1.tar.gz \
    ftp://invisible-island.net/ncurses/ncurses-5.9.tar.gz \
    ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-8.36.tar.gz \
    ftp://ftp.cwru.edu/pub/bash/readline-6.3.tar.gz \
    http://zlib.net/zlib-1.2.8.tar.gz \
    http://www.bzip.org/1.0.6/bzip2-1.0.6.tar.gz \
    http://www.sqlite.org/2014/sqlite-autoconf-3080701.tar.gz"

cd ~/packages

for url in $PACKAGE_URLS; do
    wget -N $url
done

cd ~/extract

for pkg in ~/packages/*.tar.gz; do
    tar xfz $pkg
done

(cd autoconf-2.69 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd automake-1.14.1 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd ncurses-5.9 && \
 ./configure --prefix=${FAKE_ROOT} \
     --enable-ext-mouse \
     --enable-sigwinch \
     --with-default-terminfo-dir=/usr/share/terminfo \
     --enable-ext-colors \
     --enable-widec \
    CC="gcc44" \
    CXX="g++44" \
     && \
 make && make install)

(cd pcre-8.36 && \
 ./configure --prefix=${FAKE_ROOT} \
     --enable-jit \
     --enable-utf \
    CC="gcc44" \
    CXX="g++44" \
     && \
 make && make install)

(cd readline-6.3 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd zlib-1.2.8 && ./configure --prefix=${FAKE_ROOT} && make && make install)

(cd bzip2-1.0.6 && make install PREFIX=${FAKE_ROOT})

(cd sqlite-* &&
 ./configure --prefix=${FAKE_ROOT} \
     CFLAGS="-DSQLITE_ENABLE_COLUMN_METADATA -DSQLITE_SOUNDEX" \
     && \
 make && make install)
