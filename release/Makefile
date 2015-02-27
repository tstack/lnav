
VERSION=0.7.2

outbox:
	mkdir -p $@

clean-outbox: outbox
	rm -f outbox/*

linux-vm:
	cd vagrant-static-linux && vagrant up

linux-build: linux-vm
	cd vagrant-static-linux && vagrant ssh -c /vagrant/build.sh

linux-package: clean-outbox linux-build
	mkdir -p linux-pkg/lnav-${VERSION}
	cp ../README ../NEWS linux-pkg/lnav-${VERSION}
	cp vagrant-static-linux/lnav linux-pkg/lnav-${VERSION}
	cd linux-pkg && zip -r ../outbox/lnav-${VERSION}-linux-64bit.zip lnav-${VERSION}

osx-build:
	rm -rf osx-build-dir
	mkdir -p osx-build-dir
	cd osx-build-dir && ../../configure && make -j && make distcheck -j8 && make dist-bzip2

osx-package: clean-outbox osx-build
	mkdir -p osx-pkg/lnav-${VERSION}
	cp ../README ../NEWS osx-pkg/lnav-${VERSION}
	cp osx-build-dir/src/lnav osx-pkg/lnav-${VERSION}
	cp osx-build-dir/lnav-${VERSION}.tar.gz outbox/
	cp osx-build-dir/lnav-${VERSION}.tar.bz2 outbox/
	cd osx-pkg && zip -r ../outbox/lnav-${VERSION}-os-x.zip lnav-${VERSION}

release: linux-package osx-package
