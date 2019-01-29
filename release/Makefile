
VERSION=0.8.4

SRC_VERSION=master

outbox:
	mkdir -p $@

clean-outbox: outbox
	rm -f outbox/*

PACKAGE_URLS = \
	https://ftp.gnu.org/gnu/make/make-4.2.1.tar.gz \
	ftp://ftp.gnu.org/gnu/ncurses//ncurses-5.9.tar.gz \
	ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-8.42.tar.gz \
	https://ftp.gnu.org/gnu/readline/readline-6.3.tar.gz \
	http://www.zlib.net/zlib-1.2.11.tar.gz \
	http://www.bzip.org/1.0.6/bzip2-1.0.6.tar.gz \
	https://sqlite.org/2018/sqlite-autoconf-3260000.tar.gz \
	https://www.openssl.org/source/openssl-1.0.2n.tar.gz \
	https://www.libssh2.org/download/libssh2-1.8.0.tar.gz \
	https://curl.haxx.se/download/curl-7.63.0.tar.gz

.PHONY: linux freebsd pkger download-pkgs

%-vm: %
	cd vagrant-static && vagrant up $<

download-pkgs:
	mkdir -p vagrant-static/pkgs && cd vagrant-static/pkgs && \
	for pkg in $(PACKAGE_URLS); do \
		wget -N $${pkg}; \
	done

%-build: % %-vm
	cd vagrant-static && vagrant ssh $< -c "/vagrant/build.sh ${SRC_VERSION}"

%-package: % clean-outbox %-build
	mkdir -p $<-pkg/lnav-${VERSION}
	cp ../README ../NEWS $<-pkg/lnav-${VERSION}
	mv vagrant-static/lnav $<-pkg/lnav-${VERSION}
	cd $<-pkg && zip -r ../outbox/lnav-${VERSION}-$<-64bit.zip lnav-${VERSION}
	rm -rf $<-pkg

linux-ospkg: pkger-vm linux-package
	rm -f vagrant-static/lnav\*.deb vagrant-static/lnav\*.rpm
	cd vagrant-static && vagrant ssh pkger -c "/vagrant/build-pkg.sh ${VERSION}"
	mv vagrant-static/lnav*.deb vagrant-static/lnav*.rpm outbox/

osx-build:
	rm -rf osx-build-dir
	mkdir -p osx-build-dir
	cd ../ && ./autogen.sh
	cd osx-build-dir && ../../configure --with-readline=/usr/local/opt/readline && make -j8 && make dist -j8 && make dist-bzip2

osx-package: clean-outbox osx-build
	mkdir -p osx-pkg/lnav-${VERSION}
	cp ../README ../NEWS osx-pkg/lnav-${VERSION}
	cp osx-build-dir/src/lnav osx-pkg/lnav-${VERSION}
	cp osx-build-dir/lnav-${VERSION}.tar.gz outbox/
	cp osx-build-dir/lnav-${VERSION}.tar.bz2 outbox/
	cd osx-pkg && zip -r ../outbox/lnav-${VERSION}-os-x.zip lnav-${VERSION}
	rm -rf osx-pkg

release: linux-ospkg osx-package

push:
	env LANG=UTF-8 package_cloud push tstack/lnav/ubuntu/lucid outbox/lnav*.deb
	env LANG=UTF-8 package_cloud push tstack/lnav/el/5 outbox/lnav-0.8.4-1.x86_64.rpm

clean:
	cd vagrant-static && vagrant destroy -f
	rm -rf vagrant-static/pkgs
