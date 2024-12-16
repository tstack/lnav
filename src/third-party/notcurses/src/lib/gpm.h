#ifndef NOTCURSES_GPM
#define NOTCURSES_GPM

#ifdef __cplusplus
extern "C" {
#endif

// internal header, not installed

struct tinfo;
struct ncinput;

// GPM ("General Purpose Mouse") provides an interface to mice in the Linux
// and FreeBSD consoles. The gpm server must be running; we do not attempt to
// start it. We must have been built with -DUSE_GPM.

// Returns the poll()able file descriptor associated with gpm, or -1 on failure.
int gpm_connect(struct tinfo* ti);

// Read from the gpm connection, which ought have been poll()ed. Translates
// the libgpm input to an ncinput.
int gpm_read(struct tinfo* ti, struct ncinput* ni);

int gpm_close(struct tinfo* ti);

// Returns a library-owned pointer to the libgpm client version.
const char* gpm_version(void);

#ifdef __cplusplus
}
#endif

#endif
