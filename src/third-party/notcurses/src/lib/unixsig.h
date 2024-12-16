#ifndef NOTCURSES_UNIXSIG
#define NOTCURSES_UNIXSIG

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

int setup_signals(void* nc, bool no_quit_sigs, bool no_winch_sig,
                  int(*handler)(void*));
int drop_signals(void* nc);

// block a few signals for the duration of a write to the terminal.
int block_signals(sigset_t* old_blocked_signals);
int unblock_signals(const sigset_t* old_blocked_signals);

// the alternate signal stack is a thread property; any other threads we
// create ought go ahead and install the same alternate signal stack.
void setup_alt_sig_stack(void);

#ifdef __cplusplus
}
#endif

#endif
