export NAME := lnav
SRC := ../src/$(NAME)
ifeq ($(shell arch), x86_64)
	ARCH := amd64
else ifeq ($(shell arch), aarch64)
	ARCH := arm64
else
	ARCH := all
endif
export ARCH
export VERSION := $(shell $(SRC) --version | cut -d' ' -f2 | cut -d'-' -f1,2)

export PKG := $(NAME)_$(VERSION)_$(ARCH).deb


.PHONY: build clean fake-push

CFG = $(wildcard scripts/*.template)
TGS = $(CFG:scripts/%.template=$(NAME)/DEBIAN/%)
BIN = $(NAME)/usr/local/bin/$(NAME)
HLP = $(NAME)/usr/share/doc/lnav/help.txt.gz
MAN = $(NAME)/usr/share/man/man1/lnav.1.gz

build: $(PKG)
	@echo "==> $(PKG) has been built successfully"


$(BIN): $(SRC)
	@mkdir -p $(shell dirname $@)
	cp $? $@
	strip -s $@

$(HLP): ../src/help.txt
	@mkdir -p $(shell dirname $@)
	cp $? $(shell dirname $@)/
	gzip $(subst .gz,,$@)

$(MAN): ../docs/build/man/lnav.1
	@mkdir -p $(shell dirname $@)
	cp $? $(shell dirname $@)/
	gzip $(subst .gz,,$@)

ALL = $(BIN) $(HLP) $(MAN)


$(NAME)/DEBIAN/%: scripts/%.template
	@mkdir -p $(shell dirname $@)
	cat $? | envsubst '$${NAME} $${VERSION} $${ARCH}' > $@
	@if [ "$(@F)" != "control" ]; then chmod +x $@; fi

ALL += $(TGS)


clean:
	rm -rf $(NAME) *.deb 2>/dev/null

$(PKG): $(ALL)
	fakeroot dpkg-deb -Zxz -b $(NAME) $(PKG)

push: $(PKG)
	curl -u "apt-master:inflight28a-aptmaster" -H "Content-Type: multipart/form-data" --data-binary "@$(PKG)" "https://nexus.everhub.aero/repository/ifd-apt/"
	touch push
	@echo "==> $(PKG) has been pushed to the repo successfully"

fake-push:
	touch push
