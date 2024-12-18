#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include "internal.h"
#ifdef USING_PIDFD
#error "USING_PIDFD was already defined; it should not be."
#endif
#ifdef __MINGW32__
#include <winsock2.h>
#else
#include <spawn.h>
#endif
#if (defined(__linux__))
#include <sys/wait.h>
#include <unistd.h>
// #include <linux/sched.h>
#define NCPOLLEVENTS (POLLIN | POLLRDHUP)
#if (defined(__NR_clone3) && defined(P_PIDFD) && defined(CLONE_CLEAR_SIGHAND))
#define USING_PIDFD
#endif
#else
#define NCPOLLEVENTS (POLLIN)
#endif

// release the memory and fd, but don't join the thread (since we might be
// getting called within the thread's context, on a callback).
static int
ncfdplane_destroy_inner(ncfdplane* n){
  int ret = close(n->fd);
  free(n);
  return ret;
}

// if pidfd is < 0, it won't be used in the poll()
static void
fdthread(ncfdplane* ncfp, int pidfd){
  struct pollfd pfds[2];
  memset(pfds, 0, sizeof(pfds));
  char* buf = malloc(BUFSIZ + 1);
  pfds[0].fd = ncfp->fd;
  pfds[0].events = NCPOLLEVENTS;
  const int fdcount = pidfd < 0 ? 1 : 2;
  if(fdcount > 1){
    pfds[1].fd = pidfd;
    pfds[1].events = NCPOLLEVENTS;
  }
  ssize_t r = 0;
#ifndef __MINGW32__
  while(poll(pfds, fdcount, -1) >= 0 || errno == EINTR){
#else
  while(WSAPoll(pfds, fdcount, -1) >= 0){
#endif
    if(pfds[0].revents){
      while((r = read(ncfp->fd, buf, BUFSIZ)) >= 0){
        if(r == 0){
          break;
        }
        buf[r] = '\0';
        if( (r = ncfp->cb(ncfp, buf, r, ncfp->curry)) ){
          break;
        }
        if(ncfp->destroyed){
          break;
        }
      }
      // if we're not doing follow, break out on a zero-byte read
      if(r == 0 && !ncfp->follow){
        break;
      }
    }
    if(fdcount > 1 && pfds[1].revents){
      r = 0;
      break;
    }
  }
  if(r <= 0 && !ncfp->destroyed){
    ncfp->donecb(ncfp, r == 0 ? 0 : errno, ncfp->curry);
  }
  if(ncfp->destroyed){
    ncfdplane_destroy_inner(ncfp);
  }
  free(buf);
}

static void *
ncfdplane_thread(void* vncfp){
  fdthread(vncfp, -1);
  return NULL;
}

static ncfdplane*
ncfdplane_create_internal(ncplane* n, const ncfdplane_options* opts, int fd,
                          ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn,
                          bool thread){
  if(opts->flags > 0){
    logwarn("provided unsupported flags %016" PRIx64, opts->flags);
  }
  ncfdplane* ret = malloc(sizeof(*ret));
  if(ret == NULL){
    return ret;
  }
  ret->cb = cbfxn;
  ret->donecb = donecbfxn;
  ret->follow = opts->follow;
  ret->ncp = n;
  ret->destroyed = false;
  ncplane_set_scrolling(ret->ncp, true);
  ret->fd = fd;
  ret->curry = opts->curry;
  if(thread){
    if(pthread_create(&ret->tid, NULL, ncfdplane_thread, ret)){
      free(ret);
      return NULL;
    }
  }
  return ret;
}

ncfdplane* ncfdplane_create(ncplane* n, const ncfdplane_options* opts, int fd,
                            ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn){
  ncfdplane_options zeroed = {0};
  if(!opts){
    opts = &zeroed;
  }
  if(fd < 0 || !cbfxn || !donecbfxn){
    return NULL;
  }
  return ncfdplane_create_internal(n, opts, fd, cbfxn, donecbfxn, true);
}

ncplane* ncfdplane_plane(ncfdplane* n){
  return n->ncp;
}

int ncfdplane_destroy(ncfdplane* n){
  int ret = 0;
  if(n){
    if(pthread_equal(pthread_self(), n->tid)){
      n->destroyed = true; // ncfdplane_destroy_inner() is called on thread exit
    }else{
      void* vret = NULL;
      ret |= cancel_and_join("fdplane", n->tid, &vret);
      ret |= ncfdplane_destroy_inner(n);
    }
  }
  return ret;
}

#ifndef __MINGW32__
// get 2 pipes, and ensure they're both set to close-on-exec
static int
lay_pipes(int pipes[static 2]){
#ifdef __linux__
  if(pipe2(pipes, O_CLOEXEC)){ // can't use O_NBLOCK here (affects client)
#else
  if(pipe(pipes)){
#endif
    return -1;
  }
#ifndef __linux__
  if(set_fd_cloexec(pipes[0], 1, NULL) || set_fd_cloexec(pipes[1], 1, NULL)){
    close(pipes[0]);
    close(pipes[1]);
    return -1;
  }
#endif
  return 0;
}

// ncsubproc creates a pipe, retaining the read end. it clone()s a subprocess,
// getting a pidfd. the subprocess dup2()s the write end of the pipe onto file
// descriptors 1 and 2, exec()s, and begins running. the parent creates an
// ncfdplane around the read end, involving creation of a new thread. the
// parent then returns.
static pid_t
launch_pipe_process(int* pipefd, int* pidfd, unsigned usepath,
                    const char* bin,  char* const arg[], char* const env[]){
  *pidfd = -1;
  int pipes[2];
  if(lay_pipes(pipes)){
    return -1;
  }
  pid_t p = -1;
#ifdef USING_PIDFD
  // on linux, we try to use the brand-new pidfd capability via clone3(). if
  // that fails, fall through to posix_spawn(), our only option on freebsd.
  // FIXME clone3 is not yet supported on debian sparc64/alpha as of 2020-07
  struct clone_args clargs;
  memset(&clargs, 0, sizeof(clargs));
  clargs.pidfd = (uintptr_t)pidfd;
  clargs.flags = CLONE_CLEAR_SIGHAND | CLONE_FS | CLONE_PIDFD;
  clargs.exit_signal = SIGCHLD;
  p = syscall(__NR_clone3, &clargs, sizeof(clargs));
  if(p == 0){ // child
    if(dup2(pipes[1], STDOUT_FILENO) < 0 || dup2(pipes[1], STDERR_FILENO) < 0){
      logerror("couldn't dup() %d (%s)", pipes[1], strerror(errno));
      exit(EXIT_FAILURE);
    }
    if(env){
      execvpe(bin, arg, env);
    }else if(usepath){
      execvp(bin, arg);
    }else{
      execv(bin, arg);
    }
    exit(EXIT_FAILURE);
  }else if(p < 0){
    logwarn("clone3() failed (%s), using posix_spawn()", strerror(errno));
  }
#endif
  if(p < 0){
    posix_spawn_file_actions_t factions;
    if(posix_spawn_file_actions_init(&factions)){
      logerror("couldn't initialize spawn file actions");
      return -1;
    }
    posix_spawn_file_actions_adddup2(&factions, pipes[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&factions, pipes[1], STDERR_FILENO);
    int r;
    if(usepath){
      r = posix_spawnp(&p, bin, &factions, NULL, arg, env);
    }else{
      r = posix_spawn(&p, bin, &factions, NULL, arg, env);
    }
    if(r){
      logerror("posix_spawn %s failed (%s)", bin, strerror(errno));
    }
    posix_spawn_file_actions_destroy(&factions);
  }
  if(p > 0){ // parent
    *pipefd = pipes[0];
    set_fd_nonblocking(*pipefd, 1, NULL);
  }
  return p;
}
#endif

#ifndef __MINGW32__
// nuke the just-spawned process, and reap it. called before the subprocess
// reader thread is launched (which otherwise reaps the subprocess).
static int
kill_and_wait_subproc(pid_t pid, int pidfd, int* status){
  int ret = -1;
  // on linux, we try pidfd_send_signal, if the pidfd has been defined.
  // otherwise, we fall back to regular old kill();
  if(pidfd >= 0){
#ifdef USING_PIDFD
    ret = syscall(__NR_pidfd_send_signal, pidfd, SIGKILL, NULL, 0);
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    waitid(P_PIDFD, pidfd, &info, 0);
#endif
  }
  if(ret < 0){
    kill(pid, SIGKILL);
  }
  // process ought be available immediately following waitid(), so supply
  // WNOHANG to avoid possible lockups due to weirdness
  if(pid != waitpid(pid, status, WNOHANG)){
    return -1;
  }
  return 0;
}

// need a poll on both main fd and pidfd
static void *
ncsubproc_thread(void* vncsp){
  int* status = malloc(sizeof(*status));
  if(status){
    ncsubproc* ncsp = vncsp;
    fdthread(ncsp->nfp, ncsp->pidfd);
    if(kill_and_wait_subproc(ncsp->pid, ncsp->pidfd, status)){
      *status = -1;
    }
    if(ncsp->nfp->destroyed){
      ncfdplane_destroy_inner(ncsp->nfp);
      free(ncsp);
    }
  }
  return status;
}

// this is only used if we don't have a pidfd available for poll()ing. in that
// case, we want to perform a blocking waitpid() on the pid, calling the
// completion callback when it exits (since the process exit won't necessarily
// wake up our poll()ing thread).
static void *
ncsubproc_waiter(void* vncsp){
  ncsubproc* ncsp = vncsp;
  int* status = malloc(sizeof(*status));
  pid_t pid;
  while((pid = waitpid(ncsp->pid, status, 0)) < 0 && errno == EINTR){
    ;
  }
  if(pid != ncsp->pid){
    free(status);
    return NULL;
  }
  pthread_mutex_lock(&ncsp->lock);
  ncsp->waited = true;
  pthread_mutex_unlock(&ncsp->lock);
  if(!ncsp->nfp->destroyed){
    ncsp->nfp->donecb(ncsp->nfp, *status, ncsp->nfp->curry);
  }
  return status;
}

static ncfdplane*
ncsubproc_launch(ncplane* n, ncsubproc* ret, const ncsubproc_options* opts, int fd,
                 ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn){
  ncfdplane_options popts = {
    .curry = opts->curry,
    .follow = true,
  };
  ret->nfp = ncfdplane_create_internal(n, &popts, fd, cbfxn, donecbfxn, false);
  if(ret->nfp == NULL){
    return NULL;
  }
  if(pthread_create(&ret->nfp->tid, NULL, ncsubproc_thread, ret)){
    ncfdplane_destroy_inner(ret->nfp);
    ret->nfp = NULL;
  }
  if(ret->pidfd < 0){
    // if we don't have a pidfd to throw into our poll(), we need spin up a
    // thread to call waitpid() on our pid
    if(pthread_create(&ret->waittid, NULL, ncsubproc_waiter, ret)){
      // FIXME cancel and join thread
      ncfdplane_destroy_inner(ret->nfp);
      ret->nfp = NULL;
    }
  }
  return ret->nfp;
}
#endif

// use of env implies usepath
static ncsubproc*
ncexecvpe(ncplane* n, const ncsubproc_options* opts, unsigned usepath,
          const char* bin,  char* const arg[], char* const env[],
          ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn){
  ncsubproc_options zeroed = {0};
  if(!opts){
    opts = &zeroed;
  }
  if(!cbfxn || !donecbfxn){
    return NULL;
  }
  if(opts->flags > 0){
    logwarn("provided unsupported flags %016" PRIx64, opts->flags);
  }
#ifndef __MINGW32__
  int fd = -1;
  ncsubproc* ret = malloc(sizeof(*ret));
  if(ret == NULL){
    return NULL;
  }
  memset(ret, 0, sizeof(*ret));
  ret->pid = launch_pipe_process(&fd, &ret->pidfd, usepath, bin, arg, env);
  if(ret->pid < 0){
    free(ret);
    return NULL;
  }
  if((ret->nfp = ncsubproc_launch(n, ret, opts, fd, cbfxn, donecbfxn)) == NULL){
    kill_and_wait_subproc(ret->pid, ret->pidfd, NULL);
    free(ret);
    return NULL;
  }
  return ret;
#else
  // FIXME use CreateProcess()
  (void)n;
  (void)usepath;
  (void)bin;
  (void)arg;
  (void)env;
  (void)cbfxn;
  (void)donecbfxn;
  return NULL;
#endif
}

ncsubproc* ncsubproc_createv(ncplane* n, const ncsubproc_options* opts,
                             const char* bin, const char* const arg[],
                             ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn){
  return ncexecvpe(n, opts, 0, bin, (char* const *)arg, NULL, cbfxn, donecbfxn);
}

ncsubproc* ncsubproc_createvp(ncplane* n, const ncsubproc_options* opts,
                              const char* bin, const char* const arg[],
                              ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn){
  return ncexecvpe(n, opts, 1, bin, (char* const *)arg, NULL, cbfxn, donecbfxn);
}

ncsubproc* ncsubproc_createvpe(ncplane* n, const ncsubproc_options* opts,
                       const char* bin, const char* const arg[],
                       const char* const env[],
                       ncfdplane_callback cbfxn, ncfdplane_done_cb donecbfxn){
  return ncexecvpe(n, opts, 1, bin, (char* const *)arg, (char* const*)env, cbfxn, donecbfxn);
}

int ncsubproc_destroy(ncsubproc* n){
  int ret = 0;
  if(n){
    void* vret = NULL;
//fprintf(stderr, "pid: %u pidfd: %d waittid: %u\n", n->pid, n->pidfd, n->waittid);
#ifndef __MINGW32__
#ifdef USING_PIDFD
    if(n->pidfd >= 0){
      loginfo("sending SIGKILL to pidfd %d", n->pidfd);
      if(syscall(__NR_pidfd_send_signal, n->pidfd, SIGKILL, NULL, 0)){
        kill(n->pid, SIGKILL);
      }
    }
#else
    pthread_mutex_lock(&n->lock);
    if(!n->waited){
      loginfo("sending SIGKILL to PID %d", n->pid);
      kill(n->pid, SIGKILL);
    }
    pthread_mutex_unlock(&n->lock);
#endif
#endif
    // the thread waits on the subprocess via pidfd (iff pidfd >= 0), and
    // then exits. don't try to cancel the thread in that case; rely instead on
    // killing the subprocess.
    if(n->pidfd < 0){
      pthread_cancel(n->nfp->tid);
      // shouldn't need a cancellation of waittid thanks to SIGKILL
      pthread_join(n->waittid, &vret);
    }
    if(vret == NULL){
      pthread_join(n->nfp->tid, &vret);
    }else{
      pthread_join(n->nfp->tid, NULL);
    }
    pthread_mutex_destroy(&n->lock);
    free(n);
    if(vret == NULL){
      ret = -1;
    }else if(vret != PTHREAD_CANCELED){
      ret = *(int*)vret;
      free(vret);
    }
  }
  return ret;
}

ncplane* ncsubproc_plane(ncsubproc* n){
  return n->nfp->ncp;
}

// if ttyfp is a tty, return a file descriptor extracted from it. otherwise,
// try to get the controlling terminal. otherwise, return -1.
int get_tty_fd(FILE* ttyfp){
  int fd = -1;
  if(ttyfp){
    if((fd = fileno(ttyfp)) < 0){
      logwarn("no file descriptor was available in outfp %p", ttyfp);
    }else{
      if(tty_check(fd)){
          loginfo("duplicate file descriptor %d", fd);
        fd = dup(fd);
      }else{
        loginfo("fd %d is not a TTY", fd);
        fd = -1;
      }
    }
  }
  if(fd < 0){
    fd = open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if(fd < 0){
      loginfo("couldn't open /dev/tty (%s)", strerror(errno));
    }else{
      if(!tty_check(fd)){
        loginfo("file descriptor for /dev/tty (%d) is not actually a TTY", fd);
        close(fd);
        fd = -1;
      }
    }
  }
  if(fd >= 0){
    loginfo("returning TTY fd %d", fd);
  }
  return fd;
}
