#include "internal.h"
#include "visual-details.h"
#include <stdatomic.h>

static atomic_uint_fast32_t sprixelid_nonce;

void sprixel_debug(const sprixel* s, FILE* out){
  fprintf(out, "sprixel %d (%p) %" PRIu64 "B %dx%d (%dx%d) @%d/%d state: %d\n",
          s->id, s, s->glyph.used, s->dimy, s->dimx, s->pixy, s->pixx,
          s->n ? s->n->absy : 0, s->n ? s->n->absx : 0,
          s->invalidated);
  if(s->n){
    int idx = 0;
    for(unsigned y = 0 ; y < s->dimy ; ++y){
      for(unsigned x = 0 ; x < s->dimx ; ++x){
        fprintf(out, "%d", s->n->tam[idx].state);
        ++idx;
      }
      fprintf(out, "\n");
    }
    idx = 0;
    for(unsigned y = 0 ; y < s->dimy ; ++y){
      for(unsigned x = 0 ; x < s->dimx ; ++x){
        if(s->n->tam[idx].state == SPRIXCELL_ANNIHILATED){
          if(s->n->tam[idx].auxvector){
            fprintf(out, "%03d] %p\n", idx, s->n->tam[idx].auxvector);
          }else{
            fprintf(out, "%03d] missing!\n", idx);
          }
        }
        ++idx;
      }
    }
  }
}

// doesn't splice us out of any lists, just frees
void sprixel_free(sprixel* s){
  if(s){
    loginfo("destroying sprixel %u", s->id);
    if(s->n){
      s->n->sprite = NULL;
    }
    sixelmap_free(s->smap);
    free(s->needs_refresh);
    fbuf_free(&s->glyph);
    free(s);
  }
}

sprixel* sprixel_recycle(ncplane* n){
  assert(n->sprite);
  const notcurses* nc = ncplane_notcurses_const(n);
  if(nc->tcache.pixel_implementation >= NCPIXEL_KITTY_STATIC){
    sprixel* hides = n->sprite;
    int dimy = hides->dimy;
    int dimx = hides->dimx;
    sprixel_hide(hides);
    return sprixel_alloc(n, dimy, dimx);
  }
  sixelmap_free(n->sprite->smap);
  n->sprite->smap = NULL;
  return n->sprite;
}

// store the original (absolute) coordinates from which we moved, so that
// we can invalidate them in sprite_draw().
void sprixel_movefrom(sprixel* s, int y, int x){
  if(s->invalidated != SPRIXEL_HIDE && s->invalidated != SPRIXEL_UNSEEN){
    if(s->invalidated != SPRIXEL_MOVED){
    // FIXME if we're Sixel, we need to effect any wipes that were run
    // (we normally don't because redisplaying sixel doesn't change
    // what's there--you can't "write transparency"). this is probably
    // best done by conditionally reblitting the sixel(?).
//fprintf(stderr, "SETTING TO MOVE: %d/%d was: %d\n", y, x, s->invalidated);
      s->invalidated = SPRIXEL_MOVED;
      s->movedfromy = y;
      s->movedfromx = x;
    }
  }
}

void sprixel_hide(sprixel* s){
  if(ncplane_pile(s->n) == NULL){ // ncdirect case; destroy now
    sprixel_free(s);
    return;
  }
  // otherwise, it'll be killed in the next rendering cycle.
  if(s->invalidated != SPRIXEL_HIDE){
    loginfo("marking sprixel %u hidden", s->id);
    s->invalidated = SPRIXEL_HIDE;
    s->movedfromy = ncplane_abs_y(s->n);
    s->movedfromx = ncplane_abs_x(s->n);
    // guard; might have already been replaced
    if(s->n){
      s->n->sprite = NULL;
      s->n = NULL;
    }
  }
}

// y and x are absolute coordinates.
void sprixel_invalidate(sprixel* s, int y, int x){
//fprintf(stderr, "INVALIDATING AT %d/%d\n", y, x);
  if(s->invalidated == SPRIXEL_QUIESCENT && s->n){
    int localy = y - s->n->absy;
    int localx = x - s->n->absx;
//fprintf(stderr, "INVALIDATING AT %d/%d (%d/%d) TAM: %d\n", y, x, localy, localx, s->n->tam[localy * s->dimx + localx].state);
    if(s->n->tam[localy * s->dimx + localx].state != SPRIXCELL_TRANSPARENT &&
       s->n->tam[localy * s->dimx + localx].state != SPRIXCELL_ANNIHILATED &&
       s->n->tam[localy * s->dimx + localx].state != SPRIXCELL_ANNIHILATED_TRANS){
      s->invalidated = SPRIXEL_INVALIDATED;
    }
  }
}

sprixel* sprixel_alloc(ncplane* n, int dimy, int dimx){
  sprixel* ret = malloc(sizeof(sprixel));
  if(ret == NULL){
    return NULL;
  }
  memset(ret, 0, sizeof(*ret));
  if(fbuf_init(&ret->glyph)){
    free(ret);
    return NULL;
  }
  ret->n = n;
  ret->dimy = dimy;
  ret->dimx = dimx;
  ret->id = ++sprixelid_nonce;
  ret->needs_refresh = NULL;
  if(ret->id >= 0x1000000){
    ret->id = 1;
    sprixelid_nonce = 1;
  }
//fprintf(stderr, "LOOKING AT %p (p->n = %p)\n", ret, ret->n);
  if(ncplane_pile(ret->n)){ // rendered mode
    ncpile* np = ncplane_pile(ret->n);
    if( (ret->next = np->sprixelcache) ){
      ret->next->prev = ret;
    }
    np->sprixelcache = ret;
    ret->prev = NULL;
//fprintf(stderr, "%p %p %p\n", nc->sprixelcache, ret, nc->sprixelcache->next);
  }else{ // ncdirect case
    ret->next = ret->prev = NULL;
  }
  return ret;
}

// |pixy| and |pixx| are the output pixel geometry (i.e. |pixy| must be a
// multiple of 6 for sixel). output coverage ought already have been loaded.
// takes ownership of 's' on success. frees any existing glyph.
int sprixel_load(sprixel* spx, fbuf* f, unsigned pixy, unsigned pixx,
                 int parse_start, sprixel_e state){
  assert(spx->n);
  if(&spx->glyph != f){
    fbuf_free(&spx->glyph);
    memcpy(&spx->glyph, f, sizeof(*f));
  }
  spx->invalidated = state;
  spx->pixx = pixx;
  spx->pixy = pixy;
  spx->parse_start = parse_start;
  return 0;
}

// returns 1 if already annihilated, 0 if we successfully annihilated the cell,
// or -1 if we could not annihilate the cell (i.e. we're sixel).
int sprite_wipe(const notcurses* nc, sprixel* s, int ycell, int xcell){
  assert(s->n);
  int idx = s->dimx * ycell + xcell;
  if(s->n->tam[idx].state == SPRIXCELL_TRANSPARENT){
    // need to make a transparent auxvec, because a reload will force us to
    // update said auxvec, but needn't actually change the glyph. auxvec will
    // be entirely 0s coming from pixel_trans_auxvec().
    if(s->n->tam[idx].auxvector == NULL){
      if(nc->tcache.pixel_trans_auxvec){
        s->n->tam[idx].auxvector = nc->tcache.pixel_trans_auxvec(ncplane_pile(s->n));
        if(s->n->tam[idx].auxvector == NULL){
          return -1;
        }
      }
    }
    // no need to update to INVALIDATED; no redraw is necessary
    s->n->tam[idx].state = SPRIXCELL_ANNIHILATED_TRANS;
    return 1;
  }
  if(s->n->tam[idx].state == SPRIXCELL_ANNIHILATED_TRANS ||
     s->n->tam[idx].state == SPRIXCELL_ANNIHILATED){
//fprintf(stderr, "CACHED WIPE %d %d/%d\n", s->id, ycell, xcell);
    return 0;
  }
  logdebug("wiping %p %d %d/%d", s->n->tam, idx, ycell, xcell);
  int r = nc->tcache.pixel_wipe(s, ycell, xcell);
//fprintf(stderr, "WIPED %d %d/%d ret=%d\n", s->id, ycell, xcell, r);
  // mark the cell as annihilated whether we actually scrubbed it or not,
  // so that we use this fact should we move to another frame
  s->n->tam[idx].state = SPRIXCELL_ANNIHILATED;
  assert(s->n->tam[idx].auxvector);
  return r;
}

int sprite_clear_all(const tinfo* t, fbuf* f){
  if(t->pixel_clear_all == NULL){
    return 0;
  }
  return t->pixel_clear_all(f);
}

// we don't want to seed the process-wide prng, but we do want to stir in a
// bit of randomness for our purposes. we probably ought just use platform-
// specific APIs, but for now, throw the timestamp in there, lame FIXME.
int sprite_init(tinfo* t, int fd){
  struct timeval tv;
  gettimeofday(&tv, NULL);
  int stir = (tv.tv_sec >> 3) ^ tv.tv_usec;
  sprixelid_nonce = (rand() ^ stir) % 0xffffffu;
  if(t->pixel_init == NULL){
    return 0;
  }
  return t->pixel_init(t, fd);
}

int sprixel_rescale(sprixel* spx, unsigned ncellpxy, unsigned ncellpxx){
  assert(spx->n);
  loginfo("rescaling -> %ux%u", ncellpxy, ncellpxx);
  // FIXME need adjust for sixel (scale_height)
  int nrows = (spx->pixy + (ncellpxy - 1)) / ncellpxy;
  int ncols = (spx->pixx + (ncellpxx - 1)) / ncellpxx;
  tament* ntam = create_tam(nrows, ncols);
  if(ntam == NULL){
    return -1;
  }
  for(unsigned y = 0 ; y < spx->dimy ; ++y){
    for(unsigned x = 0 ; x < spx->dimx ; ++x){
      sprite_rebuild(ncplane_notcurses(spx->n), spx, y, x);
    }
  }
  ncplane* ncopy = spx->n;
  destroy_tam(spx->n);
  // spx->n->tam has been reset, so it will not be resized herein
  ncplane_resize_simple(spx->n, nrows, ncols);
  spx->n = ncopy;
  spx->n->sprite = spx;
  spx->n->tam = ntam;
  spx->dimy = nrows;
  spx->dimx = ncols;
  return 0;
}
