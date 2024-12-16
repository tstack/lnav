#include <time.h>
#include <sys/time.h>
#include "internal.h"

typedef struct ncfadectx {
  unsigned rows;                // number of rows when allocated
  unsigned cols;                // number of columns when allocated
  int maxsteps;                 // maximum number of iterations
  unsigned maxr, maxg, maxb;    // maxima across foreground channels
  unsigned maxbr, maxbg, maxbb; // maxima across background channels
  uint64_t nanosecs_step;       // nanoseconds per iteration
  uint64_t startns;             // time fade started
  uint64_t* channels;           // all channels from the framebuffer
} ncfadectx;

int ncfadectx_iterations(const ncfadectx* nctx){
  return nctx->maxsteps;
}

// These arrays are too large to be safely placed on the stack. Get an atomic
// snapshot of all channels on the plane. While copying the snapshot, determine
// the maxima across each of the six components.
static int
alloc_ncplane_palette(ncplane* n, ncfadectx* pp, const struct timespec* ts){
  ncplane_dim_yx(n, &pp->rows, &pp->cols);
  // add an additional element for the background cell
  int size = pp->rows * pp->cols + 1;
  if((pp->channels = malloc(sizeof(*pp->channels) * size)) == NULL){
    return -1;
  }
  pp->maxr = pp->maxg = pp->maxb = 0;
  pp->maxbr = pp->maxbg = pp->maxbb = 0;
  unsigned r, g, b, br, bg, bb;
  uint64_t channels;
  unsigned y;
  for(y = 0 ; y < pp->rows ; ++y){
    for(unsigned x = 0 ; x < pp->cols ; ++x){
      channels = n->fb[nfbcellidx(n, y, x)].channels;
      pp->channels[y * pp->cols + x] = channels;
      ncchannels_fg_rgb8(channels, &r, &g, &b);
      if(r > pp->maxr){
        pp->maxr = r;
      }
      if(g > pp->maxg){
        pp->maxg = g;
      }
      if(b > pp->maxb){
        pp->maxb = b;
      }
      ncchannels_bg_rgb8(channels, &br, &bg, &bb);
      if(br > pp->maxbr){
        pp->maxbr = br;
      }
      if(bg > pp->maxbg){
        pp->maxbg = bg;
      }
      if(bb > pp->maxbb){
        pp->maxbb = bb;
      }
    }
  }
  channels = n->basecell.channels;
  pp->channels[y * pp->cols] = channels;
  ncchannels_fg_rgb8(channels, &r, &g, &b);
  if(r > pp->maxr){
    pp->maxr = r;
  }
  if(g > pp->maxg){
    pp->maxg = g;
  }
  if(b > pp->maxb){
    pp->maxb = b;
  }
  ncchannels_bg_rgb8(channels, &br, &bg, &bb);
  if(br > pp->maxbr){
    pp->maxbr = br;
  }
  if(bg > pp->maxbg){
    pp->maxbg = bg;
  }
  if(bb > pp->maxbb){
    pp->maxbb = bb;
  }
  int maxfsteps = pp->maxg > pp->maxr ? (pp->maxb > pp->maxg ? pp->maxb : pp->maxg) :
                  (pp->maxb > pp->maxr ? pp->maxb : pp->maxr);
  int maxbsteps = pp->maxbg > pp->maxbr ? (pp->maxbb > pp->maxbg ? pp->maxbb : pp->maxbg) :
                  (pp->maxbb > pp->maxbr ? pp->maxbb : pp->maxbr);
  pp->maxsteps = maxfsteps > maxbsteps ? maxfsteps : maxbsteps;
  if(pp->maxsteps == 0){
    pp->maxsteps = 1;
  }
  uint64_t nanosecs_total;
  if(ts){
    nanosecs_total = timespec_to_ns(ts);
    pp->nanosecs_step = nanosecs_total / pp->maxsteps;
    if(pp->nanosecs_step == 0){
      pp->nanosecs_step = 1;
    }
  }else{
    pp->nanosecs_step = 1;
  }
  struct timespec times;
  clock_gettime(CLOCK_MONOTONIC, &times);
  // Start time in absolute nanoseconds
  pp->startns = timespec_to_ns(&times);
  return 0;
}

int ncplane_fadein_iteration(ncplane* n, ncfadectx* nctx, int iter,
                             fadecb fader, void* curry){
  // each time through, we need look each cell back up, due to the
  // possibility of a resize event :/
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  unsigned y;
  for(y = 0 ; y < nctx->rows && y < dimy ; ++y){
    for(unsigned x = 0 ; x < nctx->cols && x < dimx; ++x){
      unsigned r, g, b;
      ncchannels_fg_rgb8(nctx->channels[nctx->cols * y + x], &r, &g, &b);
      unsigned br, bg, bb;
      ncchannels_bg_rgb8(nctx->channels[nctx->cols * y + x], &br, &bg, &bb);
      nccell* c = &n->fb[dimx * y + x];
      if(!nccell_fg_default_p(c)){
        r = r * iter / nctx->maxsteps;
        g = g * iter / nctx->maxsteps;
        b = b * iter / nctx->maxsteps;
        nccell_set_fg_rgb8(c, r, g, b);
      }
      if(!nccell_bg_default_p(c)){
        br = br * iter / nctx->maxsteps;
        bg = bg * iter / nctx->maxsteps;
        bb = bb * iter / nctx->maxsteps;
        nccell_set_bg_rgb8(c, br, bg, bb);
      }
    }
  }
  uint64_t nextwake = (iter + 1) * nctx->nanosecs_step + nctx->startns;
  struct timespec sleepspec;
  sleepspec.tv_sec = nextwake / NANOSECS_IN_SEC;
  sleepspec.tv_nsec = nextwake % NANOSECS_IN_SEC;
  int ret = 0;
  if(fader){
    ret |= fader(ncplane_notcurses(n), n, &sleepspec, curry);
  }else{
    ret |= notcurses_render(ncplane_notcurses(n));
    // clock_nanosleep() has no love for CLOCK_MONOTONIC_RAW, at least as
    // of Glibc 2.29 + Linux 5.3 (or FreeBSD 12) :/.
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleepspec, NULL);
  }
  return ret;
}

static int
ncplane_fadein_internal(ncplane* n, fadecb fader, ncfadectx* pp, void* curry){
  // Current time, sampled each iteration
  uint64_t curns;
  do{
    struct timespec times;
    clock_gettime(CLOCK_MONOTONIC, &times);
    curns = times.tv_sec * NANOSECS_IN_SEC + times.tv_nsec;
    int iter = (curns - pp->startns) / pp->nanosecs_step + 1;
    if(iter > pp->maxsteps){
      break;
    }
    int r = ncplane_fadein_iteration(n, pp, iter, fader, curry);
    if(r){
      return r;
    }
    clock_gettime(CLOCK_MONOTONIC, &times);
  }while(true);
  return 0;
}

int ncplane_fadeout_iteration(ncplane* n, ncfadectx* nctx, int iter,
                              fadecb fader, void* curry){
  unsigned br, bg, bb;
  unsigned r, g, b;
  // each time through, we need look each cell back up, due to the
  // possibility of a resize event :/
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  unsigned y;
  for(y = 0 ; y < nctx->rows && y < dimy ; ++y){
    for(unsigned x = 0 ; x < nctx->cols && x < dimx; ++x){
      nccell* c = &n->fb[dimx * y + x];
      if(!nccell_fg_default_p(c)){
        ncchannels_fg_rgb8(nctx->channels[nctx->cols * y + x], &r, &g, &b);
        r = r * (nctx->maxsteps - iter) / nctx->maxsteps;
        g = g * (nctx->maxsteps - iter) / nctx->maxsteps;
        b = b * (nctx->maxsteps - iter) / nctx->maxsteps;
        nccell_set_fg_rgb8(c, r, g, b);
      }
      if(!nccell_bg_default_p(c)){
        ncchannels_bg_rgb8(nctx->channels[nctx->cols * y + x], &br, &bg, &bb);
        br = br * (nctx->maxsteps - iter) / nctx->maxsteps;
        bg = bg * (nctx->maxsteps - iter) / nctx->maxsteps;
        bb = bb * (nctx->maxsteps - iter) / nctx->maxsteps;
        nccell_set_bg_rgb8(c, br, bg, bb);
      }
    }
  }
  nccell* c = &n->basecell;
  if(!nccell_fg_default_p(c)){
    ncchannels_fg_rgb8(nctx->channels[nctx->cols * y], &r, &g, &b);
    r = r * (nctx->maxsteps - iter) / nctx->maxsteps;
    g = g * (nctx->maxsteps - iter) / nctx->maxsteps;
    b = b * (nctx->maxsteps - iter) / nctx->maxsteps;
    nccell_set_fg_rgb8(&n->basecell, r, g, b);
  }
  if(!nccell_bg_default_p(c)){
    ncchannels_bg_rgb8(nctx->channels[nctx->cols * y], &br, &bg, &bb);
    br = br * (nctx->maxsteps - iter) / nctx->maxsteps;
    bg = bg * (nctx->maxsteps - iter) / nctx->maxsteps;
    bb = bb * (nctx->maxsteps - iter) / nctx->maxsteps;
    nccell_set_bg_rgb8(&n->basecell, br, bg, bb);
  }
  uint64_t nextwake = (iter + 1) * nctx->nanosecs_step + nctx->startns;
  struct timespec sleepspec;
  sleepspec.tv_sec = nextwake / NANOSECS_IN_SEC;
  sleepspec.tv_nsec = nextwake % NANOSECS_IN_SEC;
  int ret;
  if(fader){
    ret = fader(ncplane_notcurses(n), n, &sleepspec, curry);
  }else{
    ret = notcurses_render(ncplane_notcurses(n));
    // clock_nanosleep() has no love for CLOCK_MONOTONIC_RAW, at least as
    // of Glibc 2.29 + Linux 5.3 (or FreeBSD 12) :/.
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &sleepspec, NULL);
  }
  return ret;
}

static ncfadectx*
ncfadectx_setup_internal(ncplane* n, const struct timespec* ts){
  if(!ncplane_notcurses(n)->tcache.caps.rgb &&
     !ncplane_notcurses(n)->tcache.caps.can_change_colors){ // terminal can't fade
    return NULL;
  }
  ncfadectx* nctx = malloc(sizeof(*nctx));
  if(nctx){
    if(alloc_ncplane_palette(n, nctx, ts) == 0){
      return nctx;
    }
    free(nctx);
  }
  return NULL;
}

ncfadectx* ncfadectx_setup(ncplane* n){
  return ncfadectx_setup_internal(n, NULL);
}

void ncfadectx_free(ncfadectx* nctx){
  if(nctx){
    free(nctx->channels);
    free(nctx);
  }
}

int ncplane_fadeout(ncplane* n, const struct timespec* ts, fadecb fader, void* curry){
  ncfadectx* pp = ncfadectx_setup_internal(n, ts);
  if(!pp){
    return -1;
  }
  struct timespec times;
  ns_to_timespec(pp->startns, &times);
  do{
    uint64_t curns = times.tv_sec * NANOSECS_IN_SEC + times.tv_nsec;
    int iter = (curns - pp->startns) / pp->nanosecs_step + 1;
    if(iter > pp->maxsteps){
      break;
    }
    int r = ncplane_fadeout_iteration(n, pp, iter, fader, curry);
    if(r){
      ncfadectx_free(pp);
      return r;
    }
    clock_gettime(CLOCK_MONOTONIC, &times);
  }while(true);
  ncfadectx_free(pp);
  return 0;
}

int ncplane_fadein(ncplane* n, const struct timespec* ts, fadecb fader, void* curry){
  ncfadectx* nctx = ncfadectx_setup_internal(n, ts);
  if(nctx == NULL){
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if(fader){
      fader(ncplane_notcurses(n), n, &now, curry);
    }else{
      notcurses_render(ncplane_notcurses(n));
    }
    return -1;
  }
  int ret = ncplane_fadein_internal(n, fader, nctx, curry);
  ncfadectx_free(nctx);
  return ret;
}

int ncplane_pulse(ncplane* n, const struct timespec* ts, fadecb fader, void* curry){
  ncfadectx pp;
  int ret;
  if(!notcurses_canfade(ncplane_notcurses(n))){
    return -1;
  }
  if(alloc_ncplane_palette(n, &pp, ts)){
    return -1;
  }
  for(;;){
    ret = ncplane_fadein_internal(n, fader, &pp, curry);
    if(ret){
      break;
    }
    ret = ncplane_fadeout(n, ts, fader, curry);
    if(ret){
      break;
    }
  }
  free(pp.channels);
  return ret;
}
