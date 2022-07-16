
all-local: bin2c$(BUILD_EXEEXT)

bin2c$(BUILD_EXEEXT): bin2c.c
	$(AM_V_CC) $(CC_FOR_BUILD) $(CPPFLAGS_FOR_BUILD) $(LDFLAGS_FOR_BUILD) -g3 -o $@ $? -lz

EXTRA_DIST = \
	bin2c.c

CLEANFILES = \
	bin2c$(BUILD_EXEEXT)

distclean-local:
	$(RM_V)rm -rf *.dSYM
