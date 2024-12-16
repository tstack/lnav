#include "compat/compat.h"
#include <builddef.h>
#include <pthread.h>
#ifdef  __MINGW32__
#include <string.h>
#include <stdlib.h>
#include <synchapi.h>
#include <handleapi.h>
#include <processthreadsapi.h>
#include <stdio.h>

char* notcurses_data_dir(void){
  const char key[] = "Software\\Notcurses\\DataDir";
  DWORD plen = 0;
  LSTATUS r = RegGetValueA(HKEY_CURRENT_USER, key,
			   NULL, RRF_RT_REG_SZ, NULL,
                           NULL, &plen);
  if(r){
    goto err;
  }
  char* val = malloc(plen + 1);
  if(val == NULL){
    goto err;
  }
  r = RegGetValueA(HKEY_CURRENT_USER, key, NULL, RRF_RT_REG_SZ, NULL, val, &plen);
  if(r){
    free(val);
    goto err;
  }
  return val;

err:
  return strdup(NOTCURSES_SHARE); // fall back to build path
}

char* strndup(const char* str, size_t size){
  if(size == 0){
    return NULL;
  }
  // t is how many bytes we're copying from the old string (not including
  // any NUL terminator). it cannot be larger than strlen(str), nor can it
  // be larger than size.
  size_t t = strlen(str);
  if(t > size){
    t = size;
  }
  char* r = malloc(t + 1);
  if(r){
    if(t){
      memcpy(r, str, t);
    }
    r[t] = '\0';
  }
  return r;
}

int set_fd_nonblocking(int fd, unsigned state, unsigned* oldstate){ // FIXME
  (void)fd;
  (void)state;
  (void)oldstate;
  return 0;
}

int set_fd_cloexec(int fd, unsigned state, unsigned* oldstate){ // FIXME
  (void)fd;
  (void)state;
  (void)oldstate;
  return 0;
}

pid_t waitpid(pid_t pid, int* state, int options){ // FIXME
  (void)options;
  (void)pid;
  (void)state;
  /*
  WaitForSingleObject(pid, INFINITE);
  long unsigned pstate;
  GetExitCodeProcess(pid, &pstate);
  *state = pstate;
  CloseHandle(pid);
  return pid;
  */
  return 0;
}
#else // not windows
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#if 0
char* notcurses_data_dir(void){
  return strdup(NOTCURSES_SHARE);
}
#endif

int set_fd_nonblocking(int fd, unsigned state, unsigned* oldstate){
  int flags = fcntl(fd, F_GETFL);
  if(flags < 0){
    return -1;
  }
  if(oldstate){
    *oldstate = flags & O_NONBLOCK;
  }
  if(state){
    if(flags & O_NONBLOCK){
      return 0;
    }
    flags |= O_NONBLOCK;
  }else{
    if(!(flags & O_NONBLOCK)){
      return 0;
    }
    flags &= ~O_NONBLOCK;
  }
  if(fcntl(fd, F_SETFL, flags)){
    return -1;
  }
  return 0;
}

int set_fd_cloexec(int fd, unsigned state, unsigned* oldstate){
  int flags = fcntl(fd, F_GETFD);
  if(flags < 0){
    return -1;
  }
  if(oldstate){
    *oldstate = flags & O_CLOEXEC;
  }
  if(state){
    if(flags & O_CLOEXEC){
      return 0;
    }
    flags |= O_CLOEXEC;
  }else{
    if(!(flags & O_CLOEXEC)){
      return 0;
    }
    flags &= ~O_CLOEXEC;
  }
  if(fcntl(fd, F_SETFD, flags)){
    return -1;
  }
  return 0;
}
#if !defined(__DragonFly_version) || __DragonFly_version < 500907
// clock_nanosleep is unavailable on DragonFly BSD and Mac OS X
int clock_nanosleep(clockid_t clockid, int flags, const struct timespec *request,
                    struct timespec *remain){
  struct timespec now;
  if(clock_gettime(clockid, &now)){
    return -1;
  }
  uint64_t nowns = timespec_to_ns(&now);
  uint64_t targns = timespec_to_ns(request);
  if(flags != TIMER_ABSTIME){
    targns += nowns;
  }
  if(nowns < targns){
    uint64_t waitns = targns - nowns;
    struct timespec waitts = {
      .tv_sec = waitns / 1000000000,
      .tv_nsec = waitns % 1000000000,
    };
    return nanosleep(&waitts, remain);
  }
  return 0;

}
#endif
#endif

// initializes a pthread_cond_t to use CLOCK_MONOTONIC (as opposed to the
// default CLOCK_REALTIME) if possible. if not possible, initializes a
// regular ol' CLOCK_REALTIME condvar.
int pthread_condmonotonic_init(pthread_cond_t* cond){
  pthread_condattr_t cat;
  if(pthread_condattr_init(&cat)){
    return -1;
  }
  // FIXME we need a solution for this on macos/windows
#ifndef __APPLE__
#ifndef __MINGW32__
  if(pthread_condattr_setclock(&cat, CLOCK_MONOTONIC)){
    pthread_condattr_destroy(&cat);
    return -1;
  }
#endif
#endif
  if(pthread_cond_init(cond, &cat)){
    pthread_condattr_destroy(&cat);
    return -1;
  }
  pthread_condattr_destroy(&cat);
  return 0;
}
