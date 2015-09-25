
VERSION=0.8.0

outbox:
	mkdir -p $@

clean-outbox: outbox
	rm -f outbox/*

.PHONY: linux freebsd

%-vm: %
	cd vagrant-static && vagrant up $<


%-build: % %-vm
	cd vagrant-static && vagrant ssh $< -c /vagrant/build.sh && vagrant destroy -f $<

%-package: % clean-outbox %-build
	mkdir -p $<-pkg/lnav-${VERSION}
	cp ../README ../NEWS $<-pkg/lnav-${VERSION}
	mv vagrant-static/lnav $<-pkg/lnav-${VERSION}
	cd $<-pkg && zip -r ../outbox/lnav-${VERSION}-$<-64bit.zip lnav-${VERSION}
	rm -rf $<-pkg

osx-build:
	rm -rf osx-build-dir
	mkdir -p osx-build-dir
	cd ../ && ./autogen.sh
	cd osx-build-dir && ../../configure && make -j && make distcheck -j8 && make dist-bzip2

osx-package: clean-outbox osx-build
	mkdir -p osx-pkg/lnav-${VERSION}
	cp ../README ../NEWS osx-pkg/lnav-${VERSION}
	cp osx-build-dir/src/lnav osx-pkg/lnav-${VERSION}
	cp osx-build-dir/lnav-${VERSION}.tar.gz outbox/
	cp osx-build-dir/lnav-${VERSION}.tar.bz2 outbox/
	cd osx-pkg && zip -r ../outbox/lnav-${VERSION}-os-x.zip lnav-${VERSION}
	rm -rf osx-pkg

release: linux-package freebsd-package osx-package
