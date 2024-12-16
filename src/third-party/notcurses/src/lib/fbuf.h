#ifndef NOTCURSES_FBUF
#define NOTCURSES_FBUF

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include "compat/compat.h"
#include "logging.h"

// a growable buffer into which one can perform formatted i/o, like the
// ten thousand that came before it, and the ten trillion which shall
// come after. uses mmap (with huge pages, if possible) on unix and
// virtualalloc on windows. it can grow arbitrarily large. it does
// *not* maintain a NUL terminator, and can hold binary data.
// on Windows, we're using VirtualAlloc(). on BSD, we're using realloc().
// on Linux, we're using mmap()+mremap().

typedef struct fbuf {
  uint64_t size;
  uint64_t used;
  char* buf;
} fbuf;

// header-only so that we can test it from notcurses-tester

#ifdef MAP_POPULATE
#ifdef MAP_UNINITIALIZED
#define MAPFLAGS (MAP_POPULATE | MAP_UNINITIALIZED)
#else
#define MAPFLAGS MAP_POPULATE
#endif
#else
#ifdef MAP_UNINITIALIZED
#define MAPFLAGS MAP_UNINITIALIZED
#else
#define MAPFLAGS 0
#endif
#endif

// ensure there is sufficient room to add |n| bytes to |f|. if necessary,
// enlarge the buffer, which might move it (invalidating any references
// therein). the amount added is based on the current size (and |n|). we
// never grow larger than SIZE_MAX / 2.
static inline int
fbuf_grow(fbuf* f, size_t n){
  assert(NULL != f->buf);
  assert(0 != f->size);
  size_t size = f->size;
  if(size - f->used >= n){
    return 0; // we have enough space
  }
  while(SIZE_MAX / 2 >= size){
    size *= 2;
    if(size - f->used < n){
      continue;
    }
    void* tmp;
#ifdef __linux__
    tmp = mremap(f->buf, f->size, size, MREMAP_MAYMOVE);
    if(tmp == MAP_FAILED){
      return -1;
    }
#else
    tmp = realloc(f->buf, size);
    if(tmp == NULL){
      return -1;
    }
#endif
    f->buf = (char*)tmp; // cast for c++ callers
    f->size = size;
    return 0;
  }
  // n (or our current buffer) is too large
  return -1;
}

// prepare (a significant amount of) initial space for the fbuf.
// pass 1 for |small| if it ought be...small.
static inline int
fbuf_initgrow(fbuf* f, unsigned small){
  assert(NULL == f->buf);
  assert(0 == f->used);
  assert(0 == f->size);
  // we start with 2MiB, the huge page size on all of x86+PAE,
  // ARMv7+LPAE, ARMv8, and x86-64.
  // FIXME use GetLargePageMinimum() and sysconf
  size_t size = small ? (4096 > BUFSIZ ? 4096 : BUFSIZ) : 0x200000lu;
#if defined(__linux__)
  /*static bool hugepages_failed = false; // FIXME atomic
  if(!hugepages_failed && !small){
    // hugepages don't seem to work with mremap() =[
    // mmap(2): hugetlb results in automatic stretch out to cover hugepage
    f->buf = (char*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_HUGETLB |
                         MAP_PRIVATE | MAP_ANONYMOUS | MAPFLAGS , -1, 0);
    if(f->buf == MAP_FAILED){
      hugepages_failed = true;
      f->buf = NULL;
    }
  }
  if(f->buf == NULL){ // try again without MAP_HUGETLB */
  f->buf = (char*)mmap(NULL, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAPFLAGS , -1, 0);
  //}
  if(f->buf == MAP_FAILED){
    f->buf = NULL;
    return -1;
  }
#else
  f->buf = (char*)malloc(size);
  if(f->buf == NULL){
    return -1;
  }
#endif
  f->size = size;
  return 0;
}
#undef MAPFLAGS

// prepare f with a small initial buffer.
static inline int
fbuf_init_small(fbuf* f){
  f->used = 0;
  f->size = 0;
  f->buf = NULL;
  return fbuf_initgrow(f, 1);
}

// prepare f with a large initial buffer.
static inline int
fbuf_init(fbuf* f){
  f->used = 0;
  f->size = 0;
  f->buf = NULL;
  return fbuf_initgrow(f, 0);
}

// reset usage, but don't shrink the buffer or anything
static inline void
fbuf_reset(fbuf* f){
  f->used = 0;
}

static inline int
fbuf_reserve(fbuf* f, size_t len){
  if(fbuf_grow(f, len)){
    return -1;
  }
  return 0;
}

static inline void
fbuf_chop(fbuf* f, size_t len){
  assert(len <= f->size);
  f->used = len;
}

static inline int
fbuf_putc(fbuf* f, char c){
  if(fbuf_grow(f, 1)){
    return -1;
  }
  f->buf[f->used++] = c;
  return 1;
}

static inline int
fbuf_putn(fbuf* f, const char* s, size_t len){
  if(fbuf_grow(f, len)){
    return -1;
  }
  memcpy(f->buf + f->used, s, len);
  f->used += len;
  return len;
}

static inline int
fbuf_puts(fbuf* f, const char* s){
  size_t slen = strlen(s);
  return fbuf_putn(f, s, slen);
}

static inline int
fbuf_putint(fbuf* f, int n){
  if(fbuf_grow(f, 20)){ // 64-bit int might require up to 20 digits
    return -1;
  }
  uint64_t r = snprintf(f->buf + f->used, f->size - f->used, "%d", n);
  if(r > f->size - f->used){
    assert(r <= f->size - f->used);
    return -1; // FIXME grow?
  }
  f->used += r;
  return r;
}

static inline int
fbuf_putuint(fbuf* f, int n){
  if(fbuf_grow(f, 20)){ // 64-bit int might require up to 20 digits
    return -1;
  }
  uint64_t r = snprintf(f->buf + f->used, f->size - f->used, "%u", n);
  if(r > f->size - f->used){
    assert(r <= f->size - f->used);
    return -1; // FIXME grow?
  }
  f->used += r;
  return r;
}

// FIXME eliminate this, ideally
__attribute__ ((format (printf, 2, 3)))
static inline int
fbuf_printf(fbuf* f, const char* fmt, ...){
  if(fbuf_grow(f, BUFSIZ) < 0){
    return -1;
  }
  va_list va;
  va_start(va, fmt);
  int r = vsnprintf(f->buf + f->used, f->size - f->used, fmt, va);
  va_end(va);
  if((size_t)r >= f->size - f->used){
    return -1;
  }
  assert(r >= 0);
  f->used += r;
  return r;
}

// emit an escape; obviously you can't flush here
static inline int
fbuf_emit(fbuf* f, const char* esc){
  if(!esc){
    return -1;
  }
  if(fbuf_puts(f, esc) < 0){
    return -1;
  }
  return 0;
}

// releases the resources held by f. f itself is not freed.
static inline void
fbuf_free(fbuf* f){
  if(f){
//    logdebug("Releasing from %" PRIu32 "B (%" PRIu32 "B)", f->size, f->used);
    if(f->buf){
#if __linux__
      if(munmap(f->buf, f->size)){
        //logwarn("Error unmapping alloc (%s)", strerror(errno));
      }
#else
      free(f->buf);
#endif
      f->buf = NULL;
    }
    f->size = 0;
    f->used = 0;
  }
}

// write(2) until we've written it all. uses poll(2) to avoid spinning on
// EAGAIN, at the possible cost of some small latency.
static inline int
blocking_write(int fd, const char* buf, size_t buflen){
//fprintf(stderr, "writing %zu to %d...\n", buflen, fd);
  size_t written = 0;
  while(written < buflen){
    ssize_t w = write(fd, buf + written, buflen - written);
    if(w < 0){
      if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != EBUSY){
        logerror("Error writing out data on %d (%s)", fd, strerror(errno));
        return -1;
      }
    }else{
      written += w;
    }
    // FIXME ought probably use WSAPoll() on windows
#ifndef __MINGW32__
    if(written < buflen){
      struct pollfd pfd = {
        .fd = fd,
        .events = POLLOUT,
        .revents = 0,
      };
      poll(&pfd, 1, -1);
    }
#endif
  }
  return 0;
}

// attempt to write the contents of |f| to the FILE |fp|, if there are any
// contents. reset the fbuf either way.
static inline int
fbuf_flush(fbuf* f, FILE* fp){
  int ret = 0;
  if(f->used){
    if(fflush(fp) == EOF){
      ret = -1;
    }else if(blocking_write(fileno(fp), f->buf, f->used)){
      ret = -1;
    }
  }
  fbuf_reset(f);
  return ret;
}

// attempt to write the contents of |f| to the FILE |fp|, if there are any
// contents, and free the fbuf either way.
static inline int
fbuf_finalize(fbuf* f, FILE* fp){
  int ret = 0;
  if(f->used){
    if(fflush(fp) == EOF){
      ret = -1;
    }else if(blocking_write(fileno(fp), f->buf, f->used)){
      ret = -1;
    }
  }
  fbuf_free(f);
  return ret;
}

#ifdef __cplusplus
}
#endif

#endif
