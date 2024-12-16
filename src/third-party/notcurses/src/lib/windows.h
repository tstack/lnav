#ifndef NOTCURSES_WINDOWS
#define NOTCURSES_WINDOWS

#ifdef __cplusplus
extern "C" {
#endif

struct tinfo;

// ti has been memset to all zeroes. windows configuration is static.
int prepare_windows_terminal(struct tinfo* ti, size_t* tablelen,
                             size_t* tableused);

#ifdef __cplusplus
}
#endif

#endif
