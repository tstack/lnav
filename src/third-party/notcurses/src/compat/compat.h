#ifndef NOTCURSES_COMPAT
#define NOTCURSES_COMPAT

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>

#define NANOSECS_IN_SEC 1000000000ul

#ifdef __APPLE__
#define TIMER_ABSTIME 1
#endif

#ifdef  __MINGW32__
static inline char
path_separator(void){
  return '\\';
}
#define NL "\r\n"
#include <lmcons.h>
#include <winsock2.h>
#define tcgetattr(x, y) (0)
#define tcsetattr(x, y, z) (0)
#define ECHO      0
#define ICANON    0
#define ICRNL     0
#define INLCR     0
#define ISIG      0
#define TCSAFLUSH 0
#define TCSANOW   0
#define O_NOCTTY  0
#define O_CLOEXEC O_NOINHERIT
#define O_NONBLOCK 0
#define O_DIRECTORY 0
#define S_IFLNK 0
#define SA_SIGINFO 0
#define SA_RESETHAND 0
#define SIGQUIT 0
#define SIGCONT 0
#define SIGWINCH 0
#define SIGSTOP 0
#define gettimeofday mingw_gettimeofday
#define sigaddset(x, y)
typedef struct siginfo_t {
  int aieeee;
} siginfo_t;
#define sigset_t int
#define nl_langinfo(x) "UTF-8"
#define ppoll(w, x, y, z) WSAPoll((w), (x), (y))
pid_t waitpid(pid_t pid, int* ret, int flags);
struct winsize {
 unsigned short ws_row;
 unsigned short ws_col;
 unsigned short ws_xpixel;
 unsigned short ws_ypixel;
};
#define WNOHANG 0
#else
static inline char
path_separator(void){
  return '/';
}
#define NL "\n"
#include <poll.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#endif

// get the default data directory (heap-allocated). on unix, this is compiled
// in from builddef.h. on windows, we try to look it up in the registry (it
// ought have been written during installation), and fall back to builddef.h.
char* notcurses_data_dir(void);

// initializes a pthread_cond_t to use CLOCK_MONOTONIC (as opposed to the
// default CLOCK_REALTIME) if possible. if not possible, initializes a
// regular ol' CLOCK_REALTIME condvar. this eliminates the need for
// pthread_cond_clockwait(), which is highly nonportable.
int pthread_condmonotonic_init(pthread_cond_t* cond);

int set_fd_nonblocking(int fd, unsigned state, unsigned* oldstate);
int set_fd_cloexec(int fd, unsigned state, unsigned* oldstate);

static inline uint64_t
timespec_to_ns(const struct timespec* ts){
  return ts->tv_sec * NANOSECS_IN_SEC + ts->tv_nsec;
}

static inline struct timespec*
ns_to_timespec(uint64_t ns, struct timespec* ts){
  ts->tv_sec = ns / NANOSECS_IN_SEC;
  ts->tv_nsec = ns % NANOSECS_IN_SEC;
  return ts;
}

static inline uint64_t
clock_getns(clockid_t clockid){
  struct timespec tspec;
  if(clock_gettime(clockid, &tspec) < 0){
    return 0;
  }
  return timespec_to_ns(&tspec);
}

// compatibility wrappers for code available only on certain operating systems.
// this file is not installed, but only consumed during compilation. if we're
// on an operating system which implements a given function, it won't be built.
int clock_nanosleep(clockid_t clockid, int flags,
                    const struct timespec *request,
                    struct timespec *remain);

__attribute__ ((nonnull (2))) __attribute__ ((malloc))
static inline char*
notcurses_data_path(const char* ddir, const char* f){
  char* datadir = NULL;
  if(ddir == NULL){
    datadir = notcurses_data_dir();
    if(datadir == NULL){
      return NULL;
    }
    ddir = datadir;
  }
  const size_t dlen = strlen(ddir);
  // cast is for benefit of c++ callers, sigh
  char* path = (char*)malloc(dlen + 1 + strlen(f) + 1);
  if(path == NULL){
    free(datadir);
    return NULL;
  }
  strcpy(path, ddir);
  free(datadir);
  path[dlen] = path_separator();
  strcpy(path + dlen + 1, f);
  return path;
}

#ifdef __cplusplus
}
#else
#ifdef __MINGW32__
char* strndup(const char* str, size_t size);
#endif
#endif

#endif
