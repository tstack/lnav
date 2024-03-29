
VERSION=0.12.1

VERSION_TAG=v$(VERSION)

SRC_VERSION=master

outbox:
	mkdir -p $@

clean-outbox: outbox
	rm -f outbox/*

PACKAGE_URLS = \
	https://www.libarchive.org/downloads/libarchive-3.6.2.tar.gz \
	https://ftp.gnu.org/gnu/make/make-4.2.1.tar.gz \
	ftp://ftp.gnu.org/gnu/ncurses/ncurses-6.4.tar.gz \
	https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.42/pcre2-10.42.tar.gz \
	https://ftp.gnu.org/gnu/readline/readline-6.3.tar.gz \
	https://zlib.net/zlib-1.2.13.tar.gz \
	https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz \
	https://www.sqlite.org/2023/sqlite-autoconf-3420000.tar.gz \
	https://www.openssl.org/source/openssl-1.0.2n.tar.gz \
	https://www.libssh2.org/download/libssh2-1.11.0.tar.gz \
	https://curl.se/download/curl-8.1.2.tar.gz \
	https://tukaani.org/xz/xz-5.4.3.tar.gz

.PHONY: linux freebsd pkger download-pkgs musl

%-vm: %
	cd vagrant-static && vagrant up $<

download-pkgs:
	mkdir -p vagrant-static/pkgs && cd vagrant-static/pkgs && \
	for pkg in $(PACKAGE_URLS); do \
		if ! wget -N $${pkg}; then \
		  exit 1; \
		fi \
	done

%-build: % %-vm
	cd vagrant-static && vagrant ssh $< -c "/vagrant/build.sh ${SRC_VERSION}"

%-package: % clean-outbox %-build
	mkdir -p $<-pkg/lnav-${VERSION}
	cp ../README ../NEWS.md $<-pkg/lnav-${VERSION}
	mv vagrant-static/lnav $<-pkg/lnav-${VERSION}
	cd $<-pkg && zip -r ../outbox/lnav-${VERSION}-x86_64-linux-$<.zip lnav-${VERSION}
	rm -rf $<-pkg

linux-ospkg: pkger-vm linux-package
	rm -f vagrant-static/lnav\*.deb vagrant-static/lnav\*.rpm
	cd vagrant-static && vagrant ssh pkger -c "/vagrant/build-pkg.sh ${VERSION}"
	mv vagrant-static/lnav*.deb vagrant-static/lnav*.rpm outbox/

osx-build:
	rm -rf osx-build-dir
	mkdir -p osx-build-dir
	cd ../ && ./autogen.sh
	cd osx-build-dir && \
	  ../../configure --enable-static \
	  --with-readline=/usr/local/opt/readline \
	  --with-sqlite3=/usr/local/opt/sqlite \
	  --with-libarchive=/usr/local/opt/libarchive \
	  "CXXFLAGS=-I/usr/local/opt/ncurses/include -g1 -O2" \
	  "CFLAGS=-I/usr/local/opt/ncurses/include -g1 -O2" \
	  "LDFLAGS=-L/usr/local/opt/ncurses/lib -L/usr/local/opt/xz/lib" \
	  "LIBS=-llzma -lzstd -lbrotlidec-static -liconv -llz4" \
	  && make -j8 && make dist -j8 && make dist-bzip2

osx-package: clean-outbox osx-build
	mkdir -p osx-pkg/lnav-${VERSION}
	git pull --rebase
	cp ../README ../NEWS.md osx-pkg/lnav-${VERSION}
	cp osx-build-dir/src/lnav osx-pkg/lnav-${VERSION}
	cp osx-build-dir/lnav-${VERSION}.tar.gz outbox/
	cp osx-build-dir/lnav-${VERSION}.tar.bz2 outbox/
	cd osx-pkg && zip -r ../outbox/lnav-${VERSION}-x86_64-macos.zip lnav-${VERSION}
	rm -rf osx-pkg

release-NEWS.md: ../NEWS.md
	sed -n "/^## lnav v${VERSION}/,/^## /p" $< | sed '$$d' > $@

release-tag: release-NEWS.md
	gh release create ${VERSION_TAG} \
		-d \
		-t "lnav v${VERSION}" \
		--notes-file release-NEWS.md

release: osx-package musl-package release-NEWS.md
	gh release edit ${VERSION_TAG} \
		-t "lnav v${VERSION}" \
		--notes-file release-NEWS.md
	gh release upload ${VERSION_TAG} outbox/*

push:
	env LANG=UTF-8 package_cloud push tstack/lnav/ubuntu/lucid outbox/lnav*.deb
	env LANG=UTF-8 package_cloud push tstack/lnav/el/5 outbox/lnav-0.12.1-1.x86_64.rpm

clean:
	cd vagrant-static && vagrant destroy -f
	rm -rf vagrant-static/pkgs

lnav.spec: lnav.spec.in makespec.sh
	./makespec.sh < lnav.spec.in > $@
