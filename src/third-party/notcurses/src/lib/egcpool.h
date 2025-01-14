#ifndef NOTCURSES_EGCPOOL
#define NOTCURSES_EGCPOOL

#include <wchar.h>
#include <errno.h>
#include <stdio.h>
#include <wctype.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unigbrk.h>
#include <unictype.h>
#include "notcurses/notcurses.h"
#include "compat/compat.h"
#include "logging.h"

#ifdef __cplusplus
extern "C" {
#endif

// cells only provide storage for a single 7-bit character. if there's anything
// more than that, it's spilled into the egcpool, and the cell is given an
// offset. when a cell is released, the memory it owned is zeroed out, and
// recognizable as use for another cell.

typedef struct egcpool {
  char* pool;         // ringbuffer of attached extension storage
  int poolsize;       // total number of bytes in pool
  int poolused;       // bytes actively used, grow when this gets too large
  int poolwrite;      // next place to *look for* a place to write
} egcpool;

#define POOL_MINIMUM_ALLOC BUFSIZ
#define POOL_MAXIMUM_BYTES (1u << 24u) // max 16MiB

static inline void
egcpool_init(egcpool* p){
  memset(p, 0, sizeof(*p));
}

static inline int
egcpool_grow(egcpool* pool, size_t len){
  size_t newsize = pool->poolsize * 2;
  if(newsize < POOL_MINIMUM_ALLOC){
    newsize = POOL_MINIMUM_ALLOC;
  }
  while(len > newsize - pool->poolsize){ // ensure we make enough space
    newsize *= 2;
  }
  if(newsize > POOL_MAXIMUM_BYTES){
    return -1;
  }
  // nasty cast here because c++ source might include this header :/
  char* tmp = (char*)realloc(pool->pool, newsize);
  if(tmp == NULL){
    return -1;
  }
  pool->pool = tmp;
  memset(pool->pool + pool->poolsize, 0, newsize - pool->poolsize);
  pool->poolsize = newsize;
  return 0;
}

// get the expected length of the encoded codepoint from the first byte of a
// utf-8 character. if the byte is illegal as a first byte, 1 is returned.
// Table 3.1B, Legal UTF8 Byte Sequences, Corrigendum #1: UTF-8 Shortest Form.
// subsequent ("continuation") bytes must start with the bit pattern 10.
static inline size_t
utf8_codepoint_length(unsigned char c){
  if(c <= 0x7f){        // 0x000000...0x00007f
    return 1;
  }else if(c <= 0xc1){  // illegal continuation byte
    return 1;
  }else if(c <= 0xdf){  // 0x000080...0x0007ff
    return 2;
  }else if(c <= 0xef){  // 0x000800...0x00ffff
    return 3;
  }else if(c <= 0xf4){  // c <= 0xf4, 0x100000...0x10ffff
    return 4;
  }else{                // illegal first byte
    return 1;
  }
}

// Eat an EGC from the UTF-8 string input, counting bytes and columns. We use
// libunistring's uc_is_grapheme_break() to segment EGCs. Writes the number of
// columns to '*colcount'. Returns the number of bytes consumed, not including
// any NUL terminator. Neither the number of bytes nor columns is necessarily
// equal to the number of decoded code points. Such are the ways of Unicode.
// uc_is_grapheme_break() wants UTF-32, which is fine, because we need wchar_t
// to use wcwidth() anyway FIXME except this doesn't work with 16-bit wchar_t!
static inline int
utf8_egc_len(const char* gcluster, int* colcount){
  size_t ret = 0;
  *colcount = 0;
  int r;
  mbstate_t mbt;
  memset(&mbt, 0, sizeof(mbt));
  wchar_t wc, prevw = 0;
  bool injoin = false;
  do{
    r = mbrtowc(&wc, gcluster, MB_LEN_MAX, &mbt);
    if(r < 0){
      // FIXME probably ought escape this somehow
      logerror("invalid UTF8: %s", gcluster);
      return -1;
    }
    if(prevw && !injoin && uc_is_grapheme_break(prevw, wc)){
      break; // starts a new EGC, exit and do not claim
    }
    int cols;
    if(uc_is_property_variation_selector(wc)){ // ends EGC
      ret += r;
      break;
    }else if(wc == L'\u200d' || injoin){ // ZWJ is iswcntrl, so check it first
      injoin = true;
      cols = 0;
    }else{
      cols = uc_width(wc, "UTF-8");
      if(cols < 0){
        injoin = false;
        if(iswspace(wc)){ // newline or tab
          *colcount = 1;
          return ret + 1;
        }
        cols = 1;
        if(iswcntrl(wc)){
          logerror("prohibited or invalid unicode: 0x%08x", (unsigned)wc);
          return -1;
        }
      }
    }
    if(*colcount == 0){
      *colcount += cols;
    }
    ret += r;
    gcluster += r;
    if(!prevw){
      prevw = wc;
    }
  }while(r);
  // FIXME what if injoin is set? incomplete EGC!
  return ret;
}

// if we're inserting a EGC of |len| bytes, ought we proactively realloc?
static inline bool
egcpool_alloc_justified(const egcpool* pool, int len){
  const int poolfree = pool->poolsize - pool->poolused;
  // proactively get more space if we have less than 10% free. this doesn't
  // guarantee that we'll have enough space to insert the string -- we could
  // theoretically have every 10th byte free, and be unable to write even a
  // two-byte egc -- so we might have to allocate after an expensive search :/.
  if(poolfree >= len && poolfree * 10 > pool->poolsize){
    return false;
  }
  return true;
}

// stash away the provided UTF8, NUL-terminated grapheme cluster. the cluster
// should not be less than 2 bytes (such a cluster should be directly stored in
// the cell). returns -1 on error, and otherwise a non-negative offset. 'ulen'
// must be the number of bytes to lift from egc (utf8_egc_len()).
__attribute__ ((nonnull (1, 2))) static inline int
egcpool_stash(egcpool* pool, const char* egc, size_t ulen){
  int len = ulen + 1; // count the NUL terminator
  if(len <= 2){ // should never be empty, nor a single byte + NUL
    return -1;
  }
  // the first time through, we don't force a grow unless we expect ourselves
  // to have too little space. once we've done a search, we do force the grow.
  // we should thus never have more than two iterations of this loop.
  bool searched = false;
  // we might have to realloc our underlying pool. it is possible that this EGC
  // is actually *in* that pool, in which case our pointer will be invalidated.
  // to be safe, duplicate prior to a realloc, and free along all paths.
  char* duplicated = NULL;
  do{
    if(egcpool_alloc_justified(pool, len) || searched){
      if(!duplicated){
        // cast (and avoidance of strndup) to facilitate c++ inclusions
        if((duplicated = (char *)malloc(ulen + 1)) == NULL){
          return -1;
        }
        memcpy(duplicated, egc, ulen);
        duplicated[ulen] = '\0';
      }
      if(egcpool_grow(pool, len) && searched){
        free(duplicated);
        return -1;
      }
      egc = duplicated;
    }
    // we now look for a place to lay out this egc. we need |len| zeroes in a
    // row. starting at pool->poolwrite, look for such a range of unused
    // memory. if we find it, write it out, and update used count. if we come
    // back to where we started, force a growth and try again.
    int curpos = pool->poolwrite;
//fprintf(stderr, "Stashing [%s] %d starting at %d\n", egc, len, curpos);
    do{
      if(curpos == pool->poolsize){
        curpos = 0;
      }
      if(pool->pool[curpos]){ // can't write if there's stuff here
        ++curpos;
      }else if(curpos && pool->pool[curpos - 1]){ // don't kill someone's NUL
        ++curpos;
      }else if(pool->poolsize - curpos < len){ // can't wrap around
        if(pool->poolwrite > curpos){
          break;
        }
        curpos = 0; // can this skip pool->poolwrite?
      }else{ // promising! let's see if there's enough space
        int need = len;
        size_t trial = curpos;
        while(--need){
          if(pool->pool[++trial]){ // alas, not enough space here
            break;
          }
        }
        if(need == 0){ // found a suitable space, copy it!
          memcpy(pool->pool + curpos, egc, len - 1);
          pool->pool[curpos + len - 1] = '\0';
          pool->poolwrite = curpos + len;
          pool->poolused += len;
          free(duplicated);
//fprintf(stderr, "Stashing AT %d\n", curpos);
          return curpos;
        }
        if(pool->poolwrite > curpos && pool->poolwrite - (len - need) < curpos){
          break;
        }
        curpos += len - need;
      }
    }while(curpos != pool->poolwrite);
  }while( (searched = !searched) );
  free(duplicated);
  assert(false);
  return -1; // should never get here
}

// remove the egc from the pool. start at offset, and zero out everything until
// we find a zero (our own NUL terminator). remove that number of bytes from
// the usedcount.
static inline void
egcpool_release(egcpool* pool, int offset){
  size_t freed = 1; // account for free(d) NUL terminator
  while(pool->pool[offset]){
    pool->pool[offset] = '\0';
    ++freed;
    ++offset;
    assert(offset < pool->poolsize);
  }
  pool->poolused -= freed;
  // FIXME ought we update pool->poolwrite?
}

static inline void
egcpool_dump(egcpool* pool){
  free(pool->pool);
  pool->pool = NULL;
  pool->poolsize = 0;
  pool->poolwrite = 0;
  pool->poolused = 0;
}

// get the offset into the egcpool for this cell's EGC. returns meaningless and
// unsafe results if called on a simple cell.
static inline uint32_t
cell_egc_idx(const nccell* c){
  return (htole(c->gcluster) & 0x00fffffflu);
}

// Is the cell a spilled (more than 4 byte) UTF8 EGC?
static inline bool
cell_extended_p(const nccell* c){
  return (htole(c->gcluster) & 0xff000000ul) == 0x01000000ul;
}

// Is the cell simple (a UTF8-encoded EGC of four bytes or fewer)?
static inline bool
cell_simple_p(const nccell* c){
  return !cell_extended_p(c);
}

// only applies to complex cells, do not use on simple cells
__attribute__ ((__returns_nonnull__)) static inline const char*
egcpool_extended_gcluster(const egcpool* pool, const nccell* c) {
  assert(cell_extended_p(c));
  uint32_t idx = cell_egc_idx(c);
  return pool->pool + idx;
}

// Duplicate the contents of EGCpool 'src' onto another, wiping out any prior
// contents in 'dst'.
static inline int
egcpool_dup(egcpool* dst, const egcpool* src){
  if(src->pool){
    char* tmp;
    if((tmp = (char*)realloc(dst->pool, src->poolsize)) == NULL){
      return -1;
    }
    dst->pool = tmp;
    memcpy(dst->pool, src->pool, src->poolsize);
  }
  dst->poolsize = src->poolsize;
  dst->poolused = src->poolused;
  dst->poolwrite = src->poolwrite;
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif
