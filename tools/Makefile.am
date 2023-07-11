
all-local: bin2c$(BUILD_EXEEXT)

if CROSS_COMPILING
bin2c$(BUILD_EXEEXT): bin2c.c
	$(AM_V_CC) $(CC_FOR_BUILD) $(CPPFLAGS_FOR_BUILD) $(LDFLAGS_FOR_BUILD) -g3 -o $@ $? -lz
else
bin2c: bin2c.c
	$(AM_V_CC) $(CC) $(CPPFLAGS) $(LDFLAGS) -g3 -o $@ $? -lz
endif

EXTRA_DIST = \
	bin2c.c

CLEANFILES = \
	bin2c$(BUILD_EXEEXT)

dist-hook: bin2c$(BUILD_EXEEXT)

distclean-local:
	$(RM_V)rm -rf *.dSYM
