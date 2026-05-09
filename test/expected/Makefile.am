# Bulk-glob expected outputs into the dist tarball at dist time
# instead of listing each file in dist_noinst_DATA.  With ~2000
# fixtures, the explicit list expanded to ~146KB of recipe text and
# tripped Linux's 128KB MAX_ARG_STRLEN limit when make spawned the
# distdir-am shell, producing `Argument list too long` from
# `/bin/bash`.  The dist-hook recipe stays small (literal globs) while
# still copying every .out / .err into the distdir.
EXTRA_DIST =

dist-hook:
	cp $(srcdir)/*.out $(distdir)/ 2>/dev/null || :
	cp $(srcdir)/*.err $(distdir)/ 2>/dev/null || :
