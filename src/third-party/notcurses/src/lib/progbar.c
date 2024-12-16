#include <string.h>
#include "internal.h"

ncprogbar* ncprogbar_create(ncplane* n, const ncprogbar_options* opts){
  ncprogbar_options default_opts;
  if(opts == NULL){
    memset(&default_opts, 0, sizeof(default_opts));
    opts = &default_opts;
  }
  if(opts->flags > (NCPROGBAR_OPTION_RETROGRADE << 1u)){
    logwarn("invalid flags %016" PRIx64, opts->flags);
  }
  ncprogbar* ret = malloc(sizeof(*ret));
  if(ret == NULL){
    ncplane_destroy(n);
    return NULL;
  }
  ret->ncp = n;
  ret->ulchannel = opts->ulchannel;
  ret->urchannel = opts->urchannel;
  ret->blchannel = opts->blchannel;
  ret->brchannel = opts->brchannel;
  ret->retrograde = opts->flags & NCPROGBAR_OPTION_RETROGRADE;
  if(ncplane_set_widget(n, ret, (void(*)(void*))ncprogbar_destroy)){
    ncplane_destroy(n);
    free(ret);
    return NULL;
  }
  return ret;
}

ncplane* ncprogbar_plane(ncprogbar* n){
  return n->ncp;
}

// we never explicitly draw full blocks, as any such cell is already
// handled (and at higher resolution) by the high-res gradient.
static const char right_egcs[8][5] = {
  " ", "🮇", "🮇", "🮈", "▐", "🮉", "🮊", "🮋",
};

static const char left_egcs[8][5] = {
  " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉",
};

static const char down_egcs[8][5] = {
  " ", "▔", "🮂", "🮃", "▀", "🮄", "🮅", "🮆",
};

static const char up_egcs[8][5] = {
  " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇",
};

static int
progbar_redraw(ncprogbar* n){
  struct ncplane* ncp = ncprogbar_plane(n);
  // get current dimensions; they might have changed
  unsigned dimy, dimx;
  ncplane_dim_yx(ncp, &dimy, &dimx);
  const bool horizontal = dimx > dimy;
  int range, delt, pos;
  const char* egcs;
  uint32_t ul, ur, bl, br;
  if(horizontal){
    range = dimx;
    delt = 1;
    pos = 0;
    if(n->retrograde){
      egcs = *right_egcs;
      ul = n->urchannel; ur = n->brchannel;
      bl = n->ulchannel; br = n->blchannel;
    }else{
      egcs = *left_egcs;
      ul = n->blchannel; ur = n->ulchannel;
      bl = n->brchannel; br = n->urchannel;
    }
  }else{
    range = dimy;
    delt = -1;
    pos = range - 1;
    if(n->retrograde){
      egcs = *down_egcs;
      ul = n->brchannel; ur = n->blchannel;
      bl = n->urchannel; br = n->ulchannel;
    }else{
      egcs = *up_egcs;
      ul = n->ulchannel; ur = n->urchannel;
      bl = n->blchannel; br = n->brchannel;
    }
  }
  ncplane_home(ncp);
  if(notcurses_canutf8(ncplane_notcurses(ncp))){
    if(ncplane_gradient2x1(ncp, -1, -1, 0, 0, ul, ur, bl, br) <= 0){
      return -1;
    }
  }else{
    if(ncplane_gradient(ncp, -1, -1, 0, 0, " ", 0, ul, ur, bl, br) <= 0){
      return -1;
    }
  }
  if(n->retrograde){
    delt *= -1;
    if(pos){
      pos = 0;
    }else{
      pos = range - 1;
    }
  }
  double eachcell = (1.0 / range); // how much each cell is worth
  double chunk = n->progress;
  const int chunks = n->progress / eachcell;
  chunk -= eachcell * chunks;
  pos += delt * chunks;
  if(pos >= range){
    return 0;
  }
  if(pos < 0){
    return 0;
  }
  // at this point, we have a gradient across the entirety of the progress
  // bar. we're going to first update the active frontier, which might need
  // to cut down from a full block to a partial block. we'll then null out
  // anything beyond the frontier.
  const int egcidx = (int)(chunk / (eachcell / 8));
  const char* egc = egcs + egcidx * 5;
  if(horizontal){
    for(unsigned freepos = 0 ; freepos < dimy ; ++freepos){
      if(notcurses_canutf8(ncplane_notcurses(ncp))){
        nccell* c = ncplane_cell_ref_yx(ncp, freepos, pos);
        if(pool_blit_direct(&ncp->pool, c, egc, strlen(egc), 1) <= 0){
          return -1;
        }
        cell_set_bchannel(c, 0);
      }else{
        if(ncplane_putchar_yx(ncp, freepos, pos, ' ') <= 0){
          return -1;
        }
      }
    }
  }else{
    for(unsigned freepos = 0 ; freepos < dimx ; ++freepos){
      if(notcurses_canutf8(ncplane_notcurses(ncp))){
        nccell* c = ncplane_cell_ref_yx(ncp, pos, freepos);
        if(pool_blit_direct(&ncp->pool, c, egc, strlen(egc), 1) <= 0){
          return -1;
        }
        cell_set_bchannel(c, 0);
      }else{
        if(ncplane_putchar_yx(ncp, pos, freepos, ' ') <= 0){
          return -1;
        }
      }
    }
  }
  // we now clear out all cells beyond the frontier.
  pos += delt;
  while(pos >= 0 && pos < range){
    if(horizontal){
      for(unsigned freepos = 0 ; freepos < dimy ; ++freepos){
        nccell* c = ncplane_cell_ref_yx(ncp, freepos, pos);
        nccell_release(ncp, c);
        nccell_init(c);
      }
    }else{
      for(unsigned freepos = 0 ; freepos < dimx ; ++freepos){
        nccell* c = ncplane_cell_ref_yx(ncp, pos, freepos);
        nccell_release(ncp, c);
        nccell_init(c);
      }
    }
    pos += delt;
  }
  return 0;
}

int ncprogbar_set_progress(ncprogbar* n, double p){
//fprintf(stderr, "PROGRESS: %g\n", p);
  if(p < 0 || p > 1){
    logerror("invalid progress %g", p);
    return -1;
  }
  n->progress = p;
  return progbar_redraw(n);
}

double ncprogbar_progress(const ncprogbar* n){
  return n->progress;
}

void ncprogbar_destroy(ncprogbar* n){
  if(n){
    if(ncplane_set_widget(n->ncp, NULL, NULL) == 0){
      ncplane_destroy(n->ncp);
    }
    free(n);
  }
}
