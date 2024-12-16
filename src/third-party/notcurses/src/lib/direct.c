#include "version.h"
#include "builddef.h"
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "version.h"
#include "visual-details.h"
#include "notcurses/direct.h"
#include "internal.h"
#include "unixsig.h"

// conform to the foreground and background channels of 'channels'
static int
activate_channels(ncdirect* nc, uint64_t channels){
  if(ncchannels_fg_default_p(channels)){
    if(ncdirect_set_fg_default(nc)){
      return -1;
    }
  }else if(ncchannels_fg_palindex_p(channels)){
    if(ncdirect_set_fg_palindex(nc, ncchannels_fg_palindex(channels))){
      return -1;
    }
  }else if(ncdirect_set_fg_rgb(nc, ncchannels_fg_rgb(channels))){
    return -1;
  }
  if(ncchannels_bg_default_p(channels)){
    if(ncdirect_set_bg_default(nc)){
      return -1;
    }
  }else if(ncchannels_bg_palindex_p(channels)){
    if(ncdirect_set_bg_palindex(nc, ncchannels_bg_palindex(channels))){
      return -1;
    }
  }else if(ncdirect_set_bg_rgb(nc, ncchannels_bg_rgb(channels))){
    return -1;
  }
  return 0;
}

int ncdirect_putstr(ncdirect* nc, uint64_t channels, const char* utf8){
  if(activate_channels(nc, channels)){
    return -1;
  }
  return ncfputs(utf8, nc->ttyfp);
}

int ncdirect_putegc(ncdirect* nc, uint64_t channels, const char* utf8,
                    int* sbytes){
  int cols;
  int bytes = utf8_egc_len(utf8, &cols);
  if(bytes < 0){
    return -1;
  }
  if(sbytes){
    *sbytes = bytes;
  }
  if(activate_channels(nc, channels)){
    return -1;
  }
  if(fprintf(nc->ttyfp, "%.*s", bytes, utf8) < 0){
    return -1;
  }
  return cols;
}

int ncdirect_cursor_up(ncdirect* nc, int num){
  if(num < 0){
    logerror("requested negative move %d\n", num);
    return -1;
  }
  if(num == 0){
    return 0;
  }
  const char* cuu = get_escape(&nc->tcache, ESCAPE_CUU);
  if(cuu){
    return term_emit(tiparm(cuu, num), nc->ttyfp, false);
  }
  return -1;
}

int ncdirect_cursor_left(ncdirect* nc, int num){
  if(num < 0){
    logerror("requested negative move %d\n", num);
    return -1;
  }
  if(num == 0){
    return 0;
  }
  const char* cub = get_escape(&nc->tcache, ESCAPE_CUB);
  if(cub){
    return term_emit(tiparm(cub, num), nc->ttyfp, false);
  }
  return -1;
}

int ncdirect_cursor_right(ncdirect* nc, int num){
  if(num < 0){
    logerror("requested negative move %d\n", num);
    return -1;
  }
  if(num == 0){
    return 0;
  }
  const char* cuf = get_escape(&nc->tcache, ESCAPE_CUF);
  if(cuf){
    return term_emit(tiparm(cuf, num), nc->ttyfp, false);
  }
  return -1; // FIXME fall back to cuf1?
}

// if we're on the last line, we need some scrolling action. rather than
// merely using cud (which doesn't reliably scroll), we emit vertical tabs.
// this has the peculiar property (in all terminals tested) of scrolling when
// necessary but performing no carriage return -- a pure line feed.
int ncdirect_cursor_down(ncdirect* nc, int num){
  if(num < 0){
    logerror("requested negative move %d\n", num);
    return -1;
  }
  if(num == 0){
    return 0;
  }
  int ret = 0;
  while(num--){
    if(ncfputc('\v', nc->ttyfp) == EOF){
      ret = -1;
      break;
    }
  }
  return ret;
}

static inline int
ncdirect_cursor_down_f(ncdirect* nc, int num, fbuf* f){
  return emit_scrolls(&nc->tcache, num, f);
}

int ncdirect_clear(ncdirect* nc){
  const char* clearscr = get_escape(&nc->tcache, ESCAPE_CLEAR);
  if(clearscr){
    return term_emit(clearscr, nc->ttyfp, true);
  }
  return -1;
}

unsigned ncdirect_dim_x(ncdirect* nc){
  unsigned x;
  if(nc->tcache.ttyfd >= 0){
    unsigned cgeo, pgeo; // don't care about either
    if(update_term_dimensions(NULL, &x, &nc->tcache, 0, &cgeo, &pgeo) == 0){
      return x;
    }
  }else{
    return 80; // lol
  }
  return 0;
}

unsigned ncdirect_dim_y(ncdirect* nc){
  unsigned y;
  if(nc->tcache.ttyfd >= 0){
    unsigned cgeo, pgeo; // don't care about either
    if(update_term_dimensions(&y, NULL, &nc->tcache, 0, &cgeo, &pgeo) == 0){
      return y;
    }
  }else{
    return 24; // lol
  }
  return 0;
}

int ncdirect_cursor_enable(ncdirect* nc){
  const char* cnorm = get_escape(&nc->tcache, ESCAPE_CNORM);
  if(cnorm){
    return term_emit(cnorm, nc->ttyfp, true);
  }
  return -1;
}

int ncdirect_cursor_disable(ncdirect* nc){
  const char* cinvis = get_escape(&nc->tcache, ESCAPE_CIVIS);
  if(cinvis){
    return term_emit(cinvis, nc->ttyfp, true);
  }
  return -1;
}

static int
cursor_yx_get(ncdirect* n, const char* u7, unsigned* y, unsigned* x){
  struct inputctx* ictx = n->tcache.ictx;
  if(ncdirect_flush(n)){
    return -1;
  }
  unsigned fakey, fakex;
  if(y == NULL){
    y = &fakey;
  }
  if(x == NULL){
    x = &fakex;
  }
  if(get_cursor_location(ictx, u7, y, x)){
    logerror("couldn't get cursor position");
    return -1;
  }
  loginfo("cursor at y=%u x=%u\n", *y, *x);
  return 0;
}

// if we're lacking hpa/vpa, *and* -1 is passed for one of x/y, *and* we've
// not got a real ttyfd, we're pretty fucked. we just punt and substitute
// 0 for that case, which hopefully only happens when running headless unit
// tests under TERM=vt100. if we need to truly rigourize things, we could
// cub/cub1 the width or cuu/cuu1 the height, then cuf/cub back? FIXME
int ncdirect_cursor_move_yx(ncdirect* n, int y, int x){
  const char* hpa = get_escape(&n->tcache, ESCAPE_HPA);
  const char* vpa = get_escape(&n->tcache, ESCAPE_VPA);
  const char* u7 = get_escape(&n->tcache, ESCAPE_U7);
  if(y == -1){ // keep row the same, horizontal move only
    if(hpa){
      return term_emit(tiparm(hpa, x), n->ttyfp, false);
    }else if(n->tcache.ttyfd >= 0 && u7){
      unsigned yprime;
      if(cursor_yx_get(n, u7, &yprime, NULL)){
        return -1;
      }
      y = yprime;
    }else{
      y = 0;
    }
  }else if(x == -1){ // keep column the same, vertical move only
    if(!vpa){
      return term_emit(tiparm(vpa, y), n->ttyfp, false);
    }else if(n->tcache.ttyfd >= 0 && u7){
      unsigned xprime;
      if(cursor_yx_get(n, u7, NULL, &xprime)){
        return -1;
      }
      x = xprime;
    }else{
      x = 0;
    }
  }
  const char* cup = get_escape(&n->tcache, ESCAPE_CUP);
  if(cup){
    return term_emit(tiparm(cup, y, x), n->ttyfp, false);
  }else if(vpa && hpa){
    if(term_emit(tiparm(hpa, x), n->ttyfp, false) == 0 &&
       term_emit(tiparm(vpa, y), n->ttyfp, false) == 0){
      return 0;
    }
  }
  return -1; // we will not be moving the cursor today
}

/*
// an algorithm to detect inverted cursor reporting on terminals 2x2 or larger:
//  * get initial cursor position / push cursor position
//  * move right using cursor-independent routines
//  * move up using cursor-independent routines
//  * get cursor position
//  * if cursor position is unchanged, either cursor reporting is broken, or
//    we started in the upper-right corner. determine the latter by checking
//    terminal dimensions. if we were in the upper-right corner, move somewhere
//    else and retry.
//  * if cursor coordinate changed in only one dimension, we were either on the
//    right side, or along the top row, but not both. determine which one, and
//    determine whether we're inverted.
//  * if both dimensions changed, determine whether we're inverted by checking
//    the change. the row ought have decreased; the column ought have increased.
//  * move back to initial position / pop cursor position
static int
detect_cursor_inversion(ncdirect* n, const char* u7, int rows, int cols, int* y, int* x){
  if(rows <= 1 || cols <= 1){ // FIXME can this be made to work in 1 dimension?
    return -1;
  }
  if(cursor_yx_get(n->tcache.ttyfd, u7, y, x)){
    return -1;
  }
  // do not use normal ncdirect_cursor_*() commands, because those go to ttyfp
  // instead of tcache.ttyfd. since we always talk directly to the terminal, we need
  // to move the cursor directly via the terminal.
  // FIXME since we're always moving 1, we could also just use cuu1 etc (which
  // i believe to be the only form implemented by Windows Terminal?...)
  const char* cuu = get_escape(&n->tcache, ESCAPE_CUU);
  const char* cuf = get_escape(&n->tcache, ESCAPE_CUF);
  const char* cub = get_escape(&n->tcache, ESCAPE_CUB);
  // FIXME do we want to use cud here, or \v like above?
  const char* cud = get_escape(&n->tcache, ESCAPE_CUD);
  if(!cud || !cub || !cuf || !cuu){
    return -1;
  }
  int movex;
  int movey;
  if(*x == cols && *y == 1){
    if(tty_emit(tiparm(cud, 1), n->tcache.ttyfd)){
      return -1;
    }
    if(tty_emit(tiparm(cub, 1), n->tcache.ttyfd)){
      return -1;
    }
    movex = 1;
    movey = -1;
  }else{
    if(tty_emit(tiparm(cuu, 1), n->tcache.ttyfd)){
      return -1;
    }
    if(tty_emit(tiparm(cuf, 1), n->tcache.ttyfd)){
      return -1;
    }
    movex = -1;
    movey = 1;
  }
  int newy, newx;
  if(cursor_yx_get(n->tcache.ttyfd, u7, &newy, &newx)){
    return -1;
  }
  if(*x == cols && *y == 1){ // need to swap values, since we moved opposite
    *x = newx;
    newx = cols;
    *y = newy;
    newy = 1;
  }
  if(tty_emit(tiparm(movex == 1 ? cuf : cub, 1), n->tcache.ttyfd)){
    return -1;
  }
  if(tty_emit(tiparm(movey == 1 ? cud : cuu, 1), n->tcache.ttyfd)){
    return -1;
  }
  if(*y == newy && *x == newx){
    return -1; // hopelessly broken
  }else if(*x == newx){
    // we only changed one, supposedly the number of rows. if we were on the
    // top row before, the reply is inverted.
    if(*y == 0){
      n->tcache.inverted_cursor = true;
    }
  }else if(*y == newy){
    // we only changed one, supposedly the number of columns. if we were on the
    // rightmost column before, the reply is inverted.
    if(*x == cols){
      n->tcache.inverted_cursor = true;
    }
  }else{
    // the row ought have decreased, and the column ought have increased. if it
    // went the other way, the reply is inverted.
    if(newy > *y && newx < *x){
      n->tcache.inverted_cursor = true;
    }
  }
  n->tcache.detected_cursor_inversion = true;
  return 0;
}

static int
detect_cursor_inversion_wrapper(ncdirect* n, const char* u7, int* y, int* x){
  // if we're not on a real terminal, there's no point in running this
  if(n->tcache.ttyfd < 0){
    return 0;
  }
  const int toty = ncdirect_dim_y(n);
  const int totx = ncdirect_dim_x(n);
  // there's an argument to be made that this ought be wrapped in sc/rc
  // (push/pop cursor), rather than undoing itself. problem is, some
  // terminals lack sc/rc (they need cursor moves to run the detection
  // algorithm in the first place), and our versions go to ttyfp instead
  // of ttyfd, as needed by cursor interrogation.
  return detect_cursor_inversion(n, u7, toty, totx, y, x);
}
*/

// no terminfo capability for this. dangerous--it involves writing controls to
// the terminal, and then reading a response.
int ncdirect_cursor_yx(ncdirect* n, unsigned* y, unsigned* x){
  // this is only meaningful for real terminals
  if(n->tcache.ttyfd < 0){
    return -1;
  }
  const char* u7 = get_escape(&n->tcache, ESCAPE_U7);
  if(u7 == NULL){
    fprintf(stderr, "Terminal doesn't support cursor reporting\n");
    return -1;
  }
  unsigned yval, xval;
  if(!y){
    y = &yval;
  }
  if(!x){
    x = &xval;
  }
  return cursor_yx_get(n, u7, y, x);
}

int ncdirect_cursor_push(ncdirect* n){
  const char* sc = get_escape(&n->tcache, ESCAPE_SC);
  if(sc){
    return term_emit(sc, n->ttyfp, false);
  }
  return -1;
}

int ncdirect_cursor_pop(ncdirect* n){
  const char* rc = get_escape(&n->tcache, ESCAPE_RC);
  if(rc){
    return term_emit(rc, n->ttyfp, false);
  }
  return -1;
}

static inline int
ncdirect_align(struct ncdirect* n, ncalign_e align, unsigned c){
  if(align == NCALIGN_LEFT){
    return 0;
  }
  unsigned cols = ncdirect_dim_x(n);
  if(c > cols){
    return 0;
  }
  if(align == NCALIGN_CENTER){
    return (cols - c) / 2;
  }else if(align == NCALIGN_RIGHT){
    return cols - c;
  }
  return INT_MAX;
}

// y is an out-only param, indicating the location where drawing started
static int
ncdirect_dump_sprixel(ncdirect* n, const ncplane* np, int xoff, unsigned* y, fbuf* f){
  unsigned dimy, dimx;
  ncplane_dim_yx(np, &dimy, &dimx);
  const unsigned toty = ncdirect_dim_y(n);
  // flush our FILE*, as we're about to use UNIX I/O (since we can't rely on
  // stdio to transfer large amounts at once).
  if(ncdirect_flush(n)){
    return -1;
  }
  if(ncdirect_cursor_yx(n, y, NULL)){
    return -1;
  }
  if(toty - dimy < *y){
    int scrolls = *y - 1;
    if(toty <= dimy){
      *y = 0;
    }else{
      *y = toty - dimy;
    }
    scrolls -= *y;
    // perform our scrolling outside of the fbuf framework, as we need it
    // to happen immediately for fbcon
    if(ncdirect_cursor_move_yx(n, *y, xoff)){
      return -1;
    }
    if(emit_scrolls(&n->tcache, scrolls, f) < 0){
      return -1;
    }
  }
  if(sprite_draw(&n->tcache, NULL, np->sprite, f, *y, xoff) < 0){
    return -1;
  }
  if(sprite_commit(&n->tcache, f, np->sprite, true)){
    return -1;
  }
  return 0;
}

static int
ncdirect_set_bg_default_f(ncdirect* nc, fbuf* f){
  if(ncdirect_bg_default_p(nc)){
    return 0;
  }
  const char* esc;
  if((esc = get_escape(&nc->tcache, ESCAPE_BGOP)) != NULL){
    if(fbuf_emit(f, esc) < 0){
      return -1;
    }
  }else if((esc = get_escape(&nc->tcache, ESCAPE_OP)) != NULL){
    if(fbuf_emit(f, esc) < 0){
      return -1;
    }
    if(!ncdirect_fg_default_p(nc)){
      if(ncdirect_set_fg_rgb_f(nc, ncchannels_fg_rgb(nc->channels), f)){
        return -1;
      }
    }
  }
  ncchannels_set_bg_default(&nc->channels);
  return 0;
}

static int
ncdirect_set_fg_default_f(ncdirect* nc, fbuf* f){
  if(ncdirect_fg_default_p(nc)){
    return 0;
  }
  const char* esc;
  if((esc = get_escape(&nc->tcache, ESCAPE_FGOP)) != NULL){
    if(fbuf_emit(f, esc) < 0){
      return -1;
    }
  }else if((esc = get_escape(&nc->tcache, ESCAPE_OP)) != NULL){
    if(fbuf_emit(f, esc) < 0){
      return -1;
    }
    if(!ncdirect_bg_default_p(nc)){
      if(ncdirect_set_bg_rgb_f(nc, ncchannels_bg_rgb(nc->channels), f)){
        return -1;
      }
    }
  }
  ncchannels_set_fg_default(&nc->channels);
  return 0;
}

static int
ncdirect_dump_cellplane(ncdirect* n, const ncplane* np, fbuf* f, int xoff){
  unsigned dimy, dimx;
  ncplane_dim_yx(np, &dimy, &dimx);
  const unsigned toty = ncdirect_dim_y(n);
  // save the existing style and colors
  const bool fgdefault = ncdirect_fg_default_p(n);
  const bool bgdefault = ncdirect_bg_default_p(n);
  const uint32_t fgrgb = ncchannels_fg_rgb(n->channels);
  const uint32_t bgrgb = ncchannels_bg_rgb(n->channels);
  for(unsigned y = 0 ; y < dimy ; ++y){
    for(unsigned x = 0 ; x < dimx ; ++x){
      uint16_t stylemask;
      uint64_t channels;
      char* egc = ncplane_at_yx(np, y, x, &stylemask, &channels);
      if(egc == NULL){
        return -1;
      }
      if(ncchannels_fg_alpha(channels) == NCALPHA_TRANSPARENT){
        ncdirect_set_fg_default_f(n, f);
      }else{
        ncdirect_set_fg_rgb_f(n, ncchannels_fg_rgb(channels), f);
      }
      if(ncchannels_bg_alpha(channels) == NCALPHA_TRANSPARENT){
        ncdirect_set_bg_default_f(n, f);
      }else{
        ncdirect_set_bg_rgb_f(n, ncchannels_bg_rgb(channels), f);
      }
//fprintf(stderr, "%03d/%03d [%s] (%03dx%03d)\n", y, x, egc, dimy, dimx);
      size_t egclen = strlen(egc);
      if(fbuf_putn(f, egclen == 0 ? " " : egc, egclen == 0 ? 1 : egclen) < 0){
        free(egc);
        return -1;
      }
      free(egc);
    }
    // yes, we want to reset colors and emit an explicit new line following
    // each line of output; this is necessary if our output is lifted out and
    // used in something e.g. paste(1).
    // FIXME replace with a SGR clear
    ncdirect_set_fg_default_f(n, f);
    ncdirect_set_bg_default_f(n, f);
    if(fbuf_printf(f, "\n%*.*s", xoff, xoff, "") < 0){
      return -1;
    }
    if(y == toty){
      if(ncdirect_cursor_down_f(n, 1, f)){
        return -1;
      }
    }
  }
  // restore the previous colors
  if(fgdefault){
    ncdirect_set_fg_default_f(n, f);
  }else{
    ncdirect_set_fg_rgb_f(n, fgrgb, f);
  }
  if(bgdefault){
    ncdirect_set_bg_default_f(n, f);
  }else{
    ncdirect_set_bg_rgb_f(n, bgrgb, f);
  }
  return 0;
}

static int
ncdirect_dump_plane(ncdirect* n, const ncplane* np, int xoff){
//fprintf(stderr, "rasterizing %dx%d+%d\n", dimy, dimx, xoff);
  if(xoff){
    if(ncdirect_cursor_move_yx(n, -1, xoff)){
      return -1;
    }
  }
  fbuf f;
  if(fbuf_init(&f)){
    return -1;
  }
  if(np->sprite){
    unsigned y;
    if(ncdirect_dump_sprixel(n, np, xoff, &y, &f)){
      fbuf_free(&f);
      return -1;
    }
    if(fbuf_finalize(&f, n->ttyfp)){
      return -1;
    }
    if(n->tcache.pixel_draw_late){
      if(n->tcache.pixel_draw_late(&n->tcache, np->sprite, y, xoff) < 0){
        return -1;
      }
    }
    int targy = y + ncplane_dim_y(np);
    const int toty = ncdirect_dim_y(n);
    if(targy > toty){
      targy = toty;
    }
    if(ncdirect_cursor_move_yx(n, targy, xoff)){
      return -1;
    }
  }else{
    if(ncdirect_dump_cellplane(n, np, &f, xoff)){
      fbuf_free(&f);
      return -1;
    }
    if(fbuf_finalize(&f, n->ttyfp)){
      return -1;
    }
  }
  return 0;
}

int ncdirect_raster_frame(ncdirect* n, ncdirectv* ncdv, ncalign_e align){
  int lenx = ncplane_dim_x(ncdv);
  int xoff = ncdirect_align(n, align, lenx);
  int r = ncdirect_dump_plane(n, ncdv, xoff);
  free_plane(ncdv);
  return r;
}

static ncdirectv*
ncdirect_render_visual(ncdirect* n, ncvisual* ncv,
                       const struct ncvisual_options* vopts){
  struct ncvisual_options defvopts = {0};
  if(!vopts){
    vopts = &defvopts;
  }
//fprintf(stderr, "OUR DATA: %p rows/cols: %d/%d outsize: %d/%d %d/%d\n", ncv->data, ncv->pixy, ncv->pixx, dimy, dimx, ymax, xmax);
//fprintf(stderr, "render %d/%d to scaling: %d\n", ncv->pixy, ncv->pixx, vopts->scaling);
  const struct blitset* bset = rgba_blitter_low(&n->tcache, vopts->scaling,
                                                !(vopts->flags & NCVISUAL_OPTION_NODEGRADE),
                                                vopts->blitter);
  if(!bset){
    return NULL;
  }
  unsigned ymax = vopts->leny / bset->height;
  unsigned xmax = vopts->lenx / bset->width;
  unsigned dimy = vopts->leny > 0 ? ymax : ncdirect_dim_y(n);
  unsigned dimx = vopts->lenx > 0 ? xmax : ncdirect_dim_x(n);
  unsigned disprows, dispcols, outy;
  if(vopts->scaling != NCSCALE_NONE && vopts->scaling != NCSCALE_NONE_HIRES){
    if(bset->geom != NCBLIT_PIXEL){
      dispcols = dimx * encoding_x_scale(&n->tcache, bset);
      disprows = dimy * encoding_y_scale(&n->tcache, bset) - 1;
      outy = disprows;
    }else{
      dispcols = dimx * n->tcache.cellpxx;
      disprows = dimy * n->tcache.cellpxy;
      clamp_to_sixelmax(&n->tcache, &disprows, &dispcols, &outy, vopts->scaling);
    }
    if(vopts->scaling == NCSCALE_SCALE || vopts->scaling == NCSCALE_SCALE_HIRES){
      scale_visual(ncv, &disprows, &dispcols);
      outy = disprows;
      if(bset->geom == NCBLIT_PIXEL){
        clamp_to_sixelmax(&n->tcache, &disprows, &dispcols, &outy, vopts->scaling);
      }
    }
  }else{
    disprows = ncv->pixy;
    dispcols = ncv->pixx;
    if(bset->geom == NCBLIT_PIXEL){
      clamp_to_sixelmax(&n->tcache, &disprows, &dispcols, &outy, vopts->scaling);
    }else{
      outy = disprows;
    }
  }
  if(bset->geom == NCBLIT_PIXEL){
    while((outy + n->tcache.cellpxy - 1) / n->tcache.cellpxy > dimy){
      outy -= n->tcache.sprixel_scale_height;
      disprows = outy;
    }
  }
//fprintf(stderr, "max: %d/%d out: %d/%d\n", ymax, xmax, outy, dispcols);
//fprintf(stderr, "render: %d/%d stride %u %p\n", ncv->pixy, ncv->pixx, ncv->rowstride, ncv->data);
  ncplane_options nopts = {
    .y = 0,
    .x = 0,
    .rows = outy / encoding_y_scale(&n->tcache, bset),
    .cols = dispcols / encoding_x_scale(&n->tcache, bset),
    .userptr = NULL,
    .name = "fake",
    .resizecb = NULL,
    .flags = 0,
  };
  if(bset->geom == NCBLIT_PIXEL){
    nopts.rows = outy / n->tcache.cellpxy + !!(outy % n->tcache.cellpxy);
    nopts.cols = dispcols / n->tcache.cellpxx + !!(dispcols % n->tcache.cellpxx);
  }
  if(ymax && nopts.rows > ymax){
    nopts.rows = ymax;
  }
  if(xmax && nopts.cols > xmax){
    nopts.cols = xmax;
  }
  struct ncplane* ncdv = ncplane_new_internal(NULL, NULL, &nopts);
  if(!ncdv){
    return NULL;
  }
  if((ncdv->tam = create_tam(ncplane_dim_y(ncdv), ncplane_dim_x(ncdv))) == NULL){
    free_plane(ncdv);
    return NULL;
  }
  blitterargs bargs = {0};
  bargs.flags = vopts->flags;
  if(vopts->flags & NCVISUAL_OPTION_ADDALPHA){
    bargs.transcolor = vopts->transcolor | 0x1000000ull;
  }
  if(bset->geom == NCBLIT_PIXEL){
    bargs.u.pixel.colorregs = n->tcache.color_registers;
    bargs.u.pixel.cellpxy = n->tcache.cellpxy;
    bargs.u.pixel.cellpxx = n->tcache.cellpxx;
    if((bargs.u.pixel.spx = sprixel_alloc(ncdv, nopts.rows, nopts.cols)) == NULL){
      free_plane(ncdv);
      return NULL;
    }
    ncdv->sprite = bargs.u.pixel.spx;
  }
  if(ncvisual_blit_internal(ncv, disprows, dispcols, ncdv, bset, &bargs)){
    free_plane(ncdv);
    return NULL;
  }
  return ncdv;
}

ncdirectv* ncdirect_render_frame(ncdirect* n, const char* file,
                                 ncblitter_e blitfxn, ncscale_e scale,
                                 int ymax, int xmax){
  if(ymax < 0 || xmax < 0){
    return NULL;
  }
  ncdirectf* ncv = ncdirectf_from_file(n, file);
  if(ncv == NULL){
    return NULL;
  }
  struct ncvisual_options vopts = {0};
  const struct blitset* bset = rgba_blitter_low(&n->tcache, scale, true, blitfxn);
  if(!bset){
    return NULL;
  }
  vopts.blitter = bset->geom;
  vopts.flags = NCVISUAL_OPTION_NODEGRADE;
  vopts.scaling = scale;
  if(ymax > 0){
    if((vopts.leny = ymax * bset->height) > ncv->pixy){
      vopts.leny = 0;
    }
  }
  if(xmax > 0){
    if((vopts.lenx = xmax * bset->width) > ncv->pixx){
      vopts.lenx = 0;
    }
  }
  ncdirectv* v = ncdirectf_render(n, ncv, &vopts);
  ncvisual_destroy(ncv);
  return v;
}

int ncdirect_render_image(ncdirect* n, const char* file, ncalign_e align,
                          ncblitter_e blitfxn, ncscale_e scale){
  ncdirectv* faken = ncdirect_render_frame(n, file, blitfxn, scale, 0, 0);
  if(!faken){
    return -1;
  }
  return ncdirect_raster_frame(n, faken, align);
}

int ncdirect_set_fg_palindex(ncdirect* nc, int pidx){
  const char* setaf = get_escape(&nc->tcache, ESCAPE_SETAF);
  if(!setaf){
    return -1;
  }
  if(ncchannels_set_fg_palindex(&nc->channels, pidx) < 0){
    return -1;
  }
  return term_emit(tiparm(setaf, pidx), nc->ttyfp, false);
}

int ncdirect_set_bg_palindex(ncdirect* nc, int pidx){
  const char* setab = get_escape(&nc->tcache, ESCAPE_SETAB);
  if(!setab){
    return -1;
  }
  if(ncchannels_set_bg_palindex(&nc->channels, pidx) < 0){
    return -1;
  }
  return term_emit(tiparm(setab, pidx), nc->ttyfp, false);
}

int ncdirect_vprintf_aligned(ncdirect* n, int y, ncalign_e align, const char* fmt, va_list ap){
  char* r = ncplane_vprintf_prep(fmt, ap);
  if(r == NULL){
    return -1;
  }
  const int len = ncstrwidth(r, NULL, NULL);
  if(len < 0){
    free(r);
    return -1;
  }
  const int x = ncdirect_align(n, align, len);
  if(ncdirect_cursor_move_yx(n, y, x)){
    free(r);
    return -1;
  }
  int ret = puts(r);
  free(r);
  if(ret == EOF){
    return -1;
  }
  return ret;
}

int ncdirect_printf_aligned(ncdirect* n, int y, ncalign_e align, const char* fmt, ...){
  va_list va;
  va_start(va, fmt);
  int ret = ncdirect_vprintf_aligned(n, y, align, fmt, va);
  va_end(va);
  return ret;
}

static int
ncdirect_stop_minimal(void* vnc){
  ncdirect* nc = vnc;
  int ret = drop_signals(nc);
  fbuf f = {0};
  if(fbuf_init_small(&f) == 0){
    ret |= reset_term_attributes(&nc->tcache, &f);
    ret |= fbuf_finalize(&f, stdout);
  }
  if(nc->tcache.ttyfd >= 0){
    if(!(nc->flags & NCDIRECT_OPTION_DRAIN_INPUT)){
      if(nc->tcache.kbdlevel){
        if(tty_emit(KKEYBOARD_POP, nc->tcache.ttyfd)){
          ret = -1;
        }
      }else{
        if(tty_emit(XTMODKEYSUNDO, nc->tcache.ttyfd)){
          ret = -1;
        }
      }
    }
    const char* cnorm = get_escape(&nc->tcache, ESCAPE_CNORM);
    if(cnorm && tty_emit(cnorm, nc->tcache.ttyfd)){
      ret = -1;
    }
    ret |= tcsetattr(nc->tcache.ttyfd, TCSANOW, nc->tcache.tpreserved);
  }
  ret |= ncdirect_flush(nc);
#ifndef __MINGW32__
  del_curterm(cur_term);
#endif
  return ret;
}

ncdirect* ncdirect_core_init(const char* termtype, FILE* outfp, uint64_t flags){
  if(outfp == NULL){
    outfp = stdout;
  }
  if(flags > (NCDIRECT_OPTION_DRAIN_INPUT << 1)){ // allow them through with warning
    logwarn("Passed unsupported flags 0x%016" PRIx64 "\n", flags);
  }
  if(termtype){
    if(putenv_term(termtype)){
      return NULL;
    }
  }
  ncdirect* ret = malloc(sizeof(ncdirect));
  if(ret == NULL){
    return ret;
  }
  memset(ret, 0, sizeof(*ret));
  if(pthread_mutex_init(&ret->stats.lock, NULL)){
    free(ret);
    return NULL;
  }
  ret->flags = flags;
  ret->ttyfp = outfp;
  if(!(flags & NCDIRECT_OPTION_INHIBIT_SETLOCALE)){
    init_lang();
  }
  const char* encoding = nl_langinfo(CODESET);
  bool utf8 = false;
  if(encoding && encoding_is_utf8(encoding)){
    utf8 = true;
    ncmetric_use_utf8();
  }
  if(setup_signals(ret, (flags & NCDIRECT_OPTION_NO_QUIT_SIGHANDLERS),
                   true, ncdirect_stop_minimal)){
    pthread_mutex_destroy(&ret->stats.lock);
    free(ret);
    return NULL;
  }
  // don't set the loglevel until we've locked in signal handling, lest we
  // change the loglevel out from under a running instance.
  if(flags & NCDIRECT_OPTION_VERY_VERBOSE){
    loglevel = NCLOGLEVEL_TRACE;
  }else if(flags & NCDIRECT_OPTION_VERBOSE){
    loglevel = NCLOGLEVEL_WARNING;
  }else{
    loglevel = NCLOGLEVEL_SILENT;
  }
  set_loglevel_from_env(&loglevel);
  int cursor_y = -1;
  int cursor_x = -1;
  if(interrogate_terminfo(&ret->tcache, ret->ttyfp, utf8, 1,
                          flags & NCDIRECT_OPTION_INHIBIT_CBREAK,
                          0, &cursor_y, &cursor_x, &ret->stats, 0, 0, 0, 0,
                          flags & NCDIRECT_OPTION_DRAIN_INPUT)){
    goto err;
  }
  if(cursor_y >= 0){
    // the u7 led the queries so that we would get a cursor position
    // unaffected by any query spill (unconsumed control sequences). move
    // us back to that location, in case there was any such spillage.
    if(ncdirect_cursor_move_yx(ret, cursor_y, cursor_x)){
      free_terminfo_cache(&ret->tcache);
      goto err;
    }
  }
  if(ncvisual_init(loglevel)){
    free_terminfo_cache(&ret->tcache);
    goto err;
  }
  unsigned cgeo, pgeo; // both are don't-cares
  update_term_dimensions(NULL, NULL, &ret->tcache, 0, &cgeo, &pgeo);
  ncdirect_set_styles(ret, 0);
  return ret;

err:
  if(ret->tcache.ttyfd >= 0){
    (void)tcsetattr(ret->tcache.ttyfd, TCSANOW, ret->tcache.tpreserved);
  }
  drop_signals(ret);
  pthread_mutex_destroy(&ret->stats.lock);
  free(ret);
  return NULL;
}

int ncdirect_stop(ncdirect* nc){
  int ret = 0;
  if(nc){
    ret |= ncdirect_stop_minimal(nc);
    free_terminfo_cache(&nc->tcache);
    if(nc->tcache.ttyfd >= 0){
      ret |= close(nc->tcache.ttyfd);
    }
    pthread_mutex_destroy(&nc->stats.lock);
    free(nc);
  }
  return ret;
}

// our new input system is fundamentally incompatible with libreadline, so we
// have to fake it ourselves. at least it saves us the dependency.
//
// if NCDIRECT_OPTION_INHIBIT_CBREAK is in play, we're not going to get the
// text until cooked mode has had its way with it, and we are essentially
// unable to do anything clever. text will be echoed, and there will be no
// line-editing keybindings, save any implemented in the line discipline.
//
// otherwise, we control echo. whenever we emit output, get our position. if
// we've changed line, assume the prompt has scrolled up, and account for
// that. we return to the prompt, clear any affected lines, and reprint what
// we have.
char* ncdirect_readline(ncdirect* n, const char* prompt){
  const char* u7 = get_escape(&n->tcache, ESCAPE_U7);
  if(!u7){ // we probably *can*, but it would be a pita; screw it
    logerror("can't readline without u7");
    return NULL;
  }
  if(n->eof){
    logerror("already got EOF");
    return NULL;
  }
  if(fprintf(n->ttyfp, "%s", prompt) < 0){
    return NULL;
  }
  unsigned dimx = ncdirect_dim_x(n);
  if(dimx == 0){
    return NULL;
  }
  // FIXME what if we're reading from redirected input, not a terminal?
  unsigned y, xstart;
  if(cursor_yx_get(n, u7, &y, &xstart)){
    return NULL;
  }
  int tline = y;
  unsigned bline = y;
  wchar_t* str;
  int wspace = BUFSIZ / sizeof(*str);
  if((str = malloc(wspace * sizeof(*str))) == NULL){
    return NULL;
  }
  int wpos = 0;  // cursor location (single-dimensional)
  int wused = 0; // number used
  str[wused++] = L'\0';
  ncinput ni;
  uint32_t id;
  unsigned oldx = xstart;
  while((id = ncdirect_get_blocking(n, &ni)) != (uint32_t)-1){
    if(ni.evtype == NCTYPE_RELEASE){
      continue;
    }
    if(id == NCKEY_EOF || id == NCKEY_ENTER || (ncinput_ctrl_p(&ni) && id == 'D')){
      if(id == NCKEY_ENTER){
        if(fputc('\n', n->ttyfp) < 0){
          free(str);
          return NULL;
        }
      }else{
        n->eof = 1;
        if(wused == 1){ // NCKEY_EOF without input returns NULL
          free(str);
          return NULL;
        }
      }
      char* ustr = ncwcsrtombs(str);
      free(str);
      return ustr;
    }else if(id == NCKEY_BACKSPACE){
      if(wused > 1){
        str[wused - 2] = L'\0';
        --wused;
      }
      --wpos;
    }else if(id == NCKEY_LEFT){
      --wpos;
    }else if(id == NCKEY_RIGHT){
      ++wpos;
    }else if(id == NCKEY_UP){
      wpos -= dimx;
    }else if(id == NCKEY_DOWN){
      wpos += dimx;
    }else if(id == 'A' && ncinput_ctrl_p(&ni)){
      wpos = 1;
    }else if(id == 'E' && ncinput_ctrl_p(&ni)){
      wpos = wused - 1;
    }else if(nckey_synthesized_p(ni.id)){
      continue;
    }else{
      if(wspace - 1 < wused){
        wspace += BUFSIZ;
        wchar_t* tmp = realloc(str, wspace * sizeof(*str));
        if(tmp == NULL){
          free(str);
          return NULL;
        }
        str = tmp;
      }
      if(wpos < wused - 1){
        memmove(str + wpos + 1, str + wpos, (wused - wpos) * sizeof(*str));
        str[wpos] = id;
        ++wused;
        ++wpos;
      }else{
        str[wused - 1] = id;
        ++wused;
        ++wpos;
        str[wused - 1] = L'\0';
      }
      // FIXME check modifiers
      unsigned x;
      if(cursor_yx_get(n, u7, &y, &x)){
        break;
      }
      if(x < oldx){
        oldx = x;
        if(--tline < 0){
          tline = 0;
        }
      }
      if(y > bline){
        bline = y;
      }
    }
    if(wpos < 0){
      wpos = 0;
    }else if(wpos > wused - 1){
      wpos = wused - 1;
    }
    // clear to end of line(s)
    const char* el = get_escape(&n->tcache, ESCAPE_EL);
    for(int i = bline ; i >= tline ; --i){
      if(ncdirect_cursor_move_yx(n, i, i > tline ? 0 : xstart)){
        break;
      }
      if(term_emit(el, n->ttyfp, false)){
        break;
      }
    }
    if(fprintf(n->ttyfp, "%ls", str) < 0){
      break;
    }
    if(wpos != wused){
      int linear = xstart + wpos;
      int ylin = linear / dimx;
      int xlin = linear % dimx;
      if(ncdirect_cursor_move_yx(n, tline + ylin, xlin)){
        break;
      }
    }
    if(fflush(n->ttyfp)){
      break;
    }
  }
  free(str);
  return NULL;
}

static inline int
ncdirect_style_emit(ncdirect* n, unsigned stylebits, fbuf* f){
  unsigned normalized = 0;
  int r = coerce_styles(f, &n->tcache, &n->stylemask, stylebits, &normalized);
  // sgr0 resets colors, so set them back up if not defaults and it was used
  if(normalized){
    // emitting an sgr resets colors. if we want to be default, that's no
    // problem, and our channels remain correct. otherwise, clear our
    // channel, and set them back up.
    if(!ncdirect_fg_default_p(n)){
      if(!ncdirect_fg_palindex_p(n)){
        uint32_t fg = ncchannels_fg_rgb(n->channels);
        ncchannels_set_fg_default(&n->channels);
        r |= ncdirect_set_fg_rgb(n, fg);
      }else{ // palette-indexed
        uint32_t fg = ncchannels_fg_palindex(n->channels);
        ncchannels_set_fg_default(&n->channels);
        r |= ncdirect_set_fg_palindex(n, fg);
      }
    }
    if(!ncdirect_bg_default_p(n)){
      if(!ncdirect_bg_palindex_p(n)){
        uint32_t bg = ncchannels_bg_rgb(n->channels);
        ncchannels_set_bg_default(&n->channels);
        r |= ncdirect_set_bg_rgb(n, bg);
      }else{ // palette-indexed
        uint32_t bg = ncchannels_bg_palindex(n->channels);
        ncchannels_set_bg_default(&n->channels);
        r |= ncdirect_set_bg_palindex(n, bg);
      }
    }
  }
  return r;
}

int ncdirect_on_styles(ncdirect* n, unsigned stylebits){
  if((stylebits & n->tcache.supported_styles) < stylebits){ // unsupported styles
    return -1;
  }
  uint32_t stylemask = n->stylemask | stylebits;
  fbuf f = {0};
  if(fbuf_init_small(&f)){
    return -1;
  }
  if(ncdirect_style_emit(n, stylemask, &f)){
    fbuf_free(&f);
    return -1;
  }
  if(fbuf_finalize(&f, n->ttyfp)){
    return -1;
  }
  return 0;
}

uint16_t ncdirect_styles(const ncdirect* n){
  return n->stylemask;
}

// turn off any specified stylebits
int ncdirect_off_styles(ncdirect* n, unsigned stylebits){
  uint32_t stylemask = n->stylemask & ~stylebits;
  fbuf f = {0};
  if(fbuf_init_small(&f)){
    return -1;
  }
  if(ncdirect_style_emit(n, stylemask, &f)){
    fbuf_free(&f);
    return -1;
  }
  if(fbuf_finalize(&f, n->ttyfp)){
    return -1;
  }
  return 0;
}

// set the current stylebits to exactly those provided
int ncdirect_set_styles(ncdirect* n, unsigned stylebits){
  if((stylebits & n->tcache.supported_styles) < stylebits){ // unsupported styles
    return -1;
  }
  uint32_t stylemask = stylebits;
  fbuf f = {0};
  if(fbuf_init_small(&f)){
    return -1;
  }
  if(ncdirect_style_emit(n, stylemask, &f)){
    fbuf_free(&f);
    return -1;
  }
  if(fbuf_finalize(&f, n->ttyfp)){
    return -1;
  }
  return 0;
}

unsigned ncdirect_palette_size(const ncdirect* nc){
  return ncdirect_capabilities(nc)->colors;
}

int ncdirect_set_fg_default(ncdirect* nc){
  if(ncdirect_fg_default_p(nc)){
    return 0;
  }
  const char* esc;
  if((esc = get_escape(&nc->tcache, ESCAPE_FGOP)) != NULL){
    if(term_emit(esc, nc->ttyfp, false)){
      return -1;
    }
  }else if((esc = get_escape(&nc->tcache, ESCAPE_OP)) != NULL){
    if(term_emit(esc, nc->ttyfp, false)){
      return -1;
    }
    if(!ncdirect_bg_default_p(nc)){
      if(ncdirect_set_bg_rgb(nc, ncchannels_bg_rgb(nc->channels))){
        return -1;
      }
    }
  }
  ncchannels_set_fg_default(&nc->channels);
  return 0;
}

int ncdirect_set_bg_default(ncdirect* nc){
  if(ncdirect_bg_default_p(nc)){
    return 0;
  }
  const char* esc;
  if((esc = get_escape(&nc->tcache, ESCAPE_BGOP)) != NULL){
    if(term_emit(esc, nc->ttyfp, false)){
      return -1;
    }
  }else if((esc = get_escape(&nc->tcache, ESCAPE_OP)) != NULL){
    if(term_emit(esc, nc->ttyfp, false)){
      return -1;
    }
    if(!ncdirect_fg_default_p(nc)){
      if(ncdirect_set_fg_rgb(nc, ncchannels_fg_rgb(nc->channels))){
        return -1;
      }
    }
  }
  ncchannels_set_bg_default(&nc->channels);
  return 0;
}

int ncdirect_hline_interp(ncdirect* n, const char* egc, unsigned len,
                          uint64_t c1, uint64_t c2){
  if(len == 0){
    logerror("passed zero length\n");
    return -1;
  }
  unsigned ur, ug, ub;
  int r1, g1, b1, r2, g2, b2;
  int br1, bg1, bb1, br2, bg2, bb2;
  ncchannels_fg_rgb8(c1, &ur, &ug, &ub);
  r1 = ur; g1 = ug; b1 = ub;
  ncchannels_fg_rgb8(c2, &ur, &ug, &ub);
  r2 = ur; g2 = ug; b2 = ub;
  ncchannels_bg_rgb8(c1, &ur, &ug, &ub);
  br1 = ur; bg1 = ug; bb1 = ub;
  ncchannels_bg_rgb8(c2, &ur, &ug, &ub);
  br2 = ur; bg2 = ug; bb2 = ub;
  int deltr = r2 - r1;
  int deltg = g2 - g1;
  int deltb = b2 - b1;
  int deltbr = br2 - br1;
  int deltbg = bg2 - bg1;
  int deltbb = bb2 - bb1;
  unsigned ret;
  bool fgdef = false, bgdef = false;
  if(ncchannels_fg_default_p(c1) && ncchannels_fg_default_p(c2)){
    if(ncdirect_set_fg_default(n)){
      return -1;
    }
    fgdef = true;
  }
  if(ncchannels_bg_default_p(c1) && ncchannels_bg_default_p(c2)){
    if(ncdirect_set_bg_default(n)){
      return -1;
    }
    bgdef = true;
  }
  for(ret = 0 ; ret < len ; ++ret){
    int r = (deltr * (int)ret) / (int)len + r1;
    int g = (deltg * (int)ret) / (int)len + g1;
    int b = (deltb * (int)ret) / (int)len + b1;
    int br = (deltbr * (int)ret) / (int)len + br1;
    int bg = (deltbg * (int)ret) / (int)len + bg1;
    int bb = (deltbb * (int)ret) / (int)len + bb1;
    if(!fgdef){
      ncdirect_set_fg_rgb8(n, r, g, b);
    }
    if(!bgdef){
      ncdirect_set_bg_rgb8(n, br, bg, bb);
    }
    if(fprintf(n->ttyfp, "%s", egc) < 0){
      logerror("error emitting egc [%s]\n", egc);
      return -1;
    }
  }
  return ret;
}

int ncdirect_vline_interp(ncdirect* n, const char* egc, unsigned len,
                          uint64_t c1, uint64_t c2){
  if(len == 0){
    logerror("passed zero length\n");
    return -1;
  }
  unsigned ur, ug, ub;
  int r1, g1, b1, r2, g2, b2;
  int br1, bg1, bb1, br2, bg2, bb2;
  ncchannels_fg_rgb8(c1, &ur, &ug, &ub);
  r1 = ur; g1 = ug; b1 = ub;
  ncchannels_fg_rgb8(c2, &ur, &ug, &ub);
  r2 = ur; g2 = ug; b2 = ub;
  ncchannels_bg_rgb8(c1, &ur, &ug, &ub);
  br1 = ur; bg1 = ug; bb1 = ub;
  ncchannels_bg_rgb8(c2, &ur, &ug, &ub);
  br2 = ur; bg2 = ug; bb2 = ub;
  int deltr = (r2 - r1) / ((int)len + 1);
  int deltg = (g2 - g1) / ((int)len + 1);
  int deltb = (b2 - b1) / ((int)len + 1);
  int deltbr = (br2 - br1) / ((int)len + 1);
  int deltbg = (bg2 - bg1) / ((int)len + 1);
  int deltbb = (bb2 - bb1) / ((int)len + 1);
  unsigned ret;
  bool fgdef = false, bgdef = false;
  if(ncchannels_fg_default_p(c1) && ncchannels_fg_default_p(c2)){
    if(ncdirect_set_fg_default(n)){
      return -1;
    }
    fgdef = true;
  }
  if(ncchannels_bg_default_p(c1) && ncchannels_bg_default_p(c2)){
    if(ncdirect_set_bg_default(n)){
      return -1;
    }
    bgdef = true;
  }
  for(ret = 0 ; ret < len ; ++ret){
    r1 += deltr;
    g1 += deltg;
    b1 += deltb;
    br1 += deltbr;
    bg1 += deltbg;
    bb1 += deltbb;
    uint64_t channels = 0;
    if(!fgdef){
      ncchannels_set_fg_rgb8(&channels, r1, g1, b1);
    }
    if(!bgdef){
      ncchannels_set_bg_rgb8(&channels, br1, bg1, bb1);
    }
    if(ncdirect_putstr(n, channels, egc) == EOF){
      return -1;
    }
    if(len - ret > 1){
      if(ncdirect_cursor_down(n, 1) || ncdirect_cursor_left(n, 1)){
        return -1;
      }
    }
  }
  return ret;
}

//  wchars: wchar_t[6] mapping to UL, UR, BL, BR, HL, VL.
//  they cannot be complex EGCs, but only a single wchar_t, alas.
int ncdirect_box(ncdirect* n, uint64_t ul, uint64_t ur,
                 uint64_t ll, uint64_t lr, const wchar_t* wchars,
                 unsigned ylen, unsigned xlen, unsigned ctlword){
  if(xlen < 2 || ylen < 2){
    return -1;
  }
  char hl[MB_LEN_MAX + 1];
  char vl[MB_LEN_MAX + 1];
  unsigned edges;
  edges = !(ctlword & NCBOXMASK_TOP) + !(ctlword & NCBOXMASK_LEFT);
  // FIXME rewrite all fprintfs as ncdirect_putstr()!
  if(edges >= box_corner_needs(ctlword)){
    if(activate_channels(n, ul)){
      return -1;
    }
    if(fprintf(n->ttyfp, "%lc", wchars[0]) < 0){
      logerror("error emitting %lc\n", wchars[0]);
      return -1;
    }
  }else{
    ncdirect_cursor_right(n, 1);
  }
  mbstate_t ps = {0};
  size_t bytes;
  if((bytes = wcrtomb(hl, wchars[4], &ps)) == (size_t)-1){
    logerror("error converting %lc\n", wchars[4]);
    return -1;
  }
  hl[bytes] = '\0';
  memset(&ps, 0, sizeof(ps));
  if((bytes = wcrtomb(vl, wchars[5], &ps)) == (size_t)-1){
    logerror("error converting %lc\n", wchars[5]);
    return -1;
  }
  vl[bytes] = '\0';
  if(!(ctlword & NCBOXMASK_TOP)){ // draw top border, if called for
    if(xlen > 2){
      if(ncdirect_hline_interp(n, hl, xlen - 2, ul, ur) < 0){
        return -1;
      }
    }
  }else{
    ncdirect_cursor_right(n, xlen - 2);
  }
  edges = !(ctlword & NCBOXMASK_TOP) + !(ctlword & NCBOXMASK_RIGHT);
  if(edges >= box_corner_needs(ctlword)){
    if(activate_channels(n, ur)){
      return -1;
    }
    if(fprintf(n->ttyfp, "%lc", wchars[1]) < 0){
      return -1;
    }
    ncdirect_cursor_left(n, xlen);
  }else{
    ncdirect_cursor_left(n, xlen - 1);
  }
  ncdirect_cursor_down(n, 1);
  // middle rows (vertical lines)
  if(ylen > 2){
    if(!(ctlword & NCBOXMASK_LEFT)){
      if(ncdirect_vline_interp(n, vl, ylen - 2, ul, ll) < 0){
        return -1;
      }
      ncdirect_cursor_right(n, xlen - 2);
      ncdirect_cursor_up(n, ylen - 3);
    }else{
      ncdirect_cursor_right(n, xlen - 1);
    }
    if(!(ctlword & NCBOXMASK_RIGHT)){
      if(ncdirect_vline_interp(n, vl, ylen - 2, ur, lr) < 0){
        return -1;
      }
      ncdirect_cursor_left(n, xlen);
    }else{
      ncdirect_cursor_left(n, xlen - 1);
    }
    ncdirect_cursor_down(n, 1);
  }
  // bottom line
  edges = !(ctlword & NCBOXMASK_BOTTOM) + !(ctlword & NCBOXMASK_LEFT);
  if(edges >= box_corner_needs(ctlword)){
    if(activate_channels(n, ll)){
      return -1;
    }
    if(fprintf(n->ttyfp, "%lc", wchars[2]) < 0){
      return -1;
    }
  }else{
    ncdirect_cursor_right(n, 1);
  }
  if(!(ctlword & NCBOXMASK_BOTTOM)){
    if(xlen > 2){
      if(ncdirect_hline_interp(n, hl, xlen - 2, ll, lr) < 0){
        return -1;
      }
    }
  }else{
    ncdirect_cursor_right(n, xlen - 2);
  }
  edges = !(ctlword & NCBOXMASK_BOTTOM) + !(ctlword & NCBOXMASK_RIGHT);
  if(edges >= box_corner_needs(ctlword)){
    if(activate_channels(n, lr)){
      return -1;
    }
    if(fprintf(n->ttyfp, "%lc", wchars[3]) < 0){
      return -1;
    }
  }
  return 0;
}

int ncdirect_rounded_box(ncdirect* n, uint64_t ul, uint64_t ur,
                         uint64_t ll, uint64_t lr,
                         unsigned ylen, unsigned xlen, unsigned ctlword){
  return ncdirect_box(n, ul, ur, ll, lr, NCBOXROUNDW, ylen, xlen, ctlword);
}

int ncdirect_double_box(ncdirect* n, uint64_t ul, uint64_t ur,
                        uint64_t ll, uint64_t lr,
                        unsigned ylen, unsigned xlen, unsigned ctlword){
  return ncdirect_box(n, ul, ur, ll, lr, NCBOXDOUBLEW, ylen, xlen, ctlword);
}

// Is our encoding UTF-8? Requires LANG being set to a UTF8 locale.
bool ncdirect_canutf8(const ncdirect* n){
  return n->tcache.caps.utf8;
}

int ncdirect_flush(const ncdirect* nc){
  return ncflush(nc->ttyfp);
}

int ncdirect_check_pixel_support(const ncdirect* n){
  if(n->tcache.pixel_draw || n->tcache.pixel_draw_late){
    return 1;
  }
  return 0;
}

int ncdirect_stream(ncdirect* n, const char* filename, ncstreamcb streamer,
                    struct ncvisual_options* vopts, void* curry){
  ncvisual* ncv = ncvisual_from_file(filename);
  if(ncv == NULL){
    return -1;
  }
  // starting position *after displaying one frame* so as to effect any
  // necessary scrolling.
  unsigned y = 0, x = 0;
  int lastid = -1;
  int thisid = -1;
  do{
    if(y > 0){
      if(x == ncdirect_dim_x(n)){
        x = 0;
        ++y;
      }
      ncdirect_cursor_up(n, y - 1);
    }
    if(x > 0){
      ncdirect_cursor_left(n, x);
    }
    ncdirectv* v = ncdirect_render_visual(n, ncv, vopts);
    if(v == NULL){
      ncvisual_destroy(ncv);
      return -1;
    }
    ncplane_dim_yx(v, &y, &x);
    if(v->sprite){
      thisid = v->sprite->id;
    }
    if(ncdirect_raster_frame(n, v, (vopts->flags & NCVISUAL_OPTION_HORALIGNED) ? vopts->x : 0)){
      ncvisual_destroy(ncv);
      return -1;
    }
    if(lastid > -1){
      if(n->tcache.pixel_remove){
        fbuf f = {0};
        fbuf_init_small(&f);
        if(n->tcache.pixel_remove(lastid, &f)){
          fbuf_free(&f);
          ncvisual_destroy(ncv);
          return -1;
        }
        if(fbuf_finalize(&f, n->ttyfp) < 0){
          ncvisual_destroy(ncv);
          return -1;
        }
      }
    }
    streamer(ncv, vopts, NULL, curry);
    lastid = thisid;
  }while(ncvisual_decode(ncv) == 0);
  ncdirect_flush(n);
  ncvisual_destroy(ncv);
  return 0;
}

ncdirectf* ncdirectf_from_file(ncdirect* n __attribute__ ((unused)),
                               const char* filename){
  return ncvisual_from_file(filename);
}

void ncdirectf_free(ncdirectf* frame){
  ncvisual_destroy(frame);
}

ncdirectv* ncdirectf_render(ncdirect* n, ncdirectf* frame, const struct ncvisual_options* vopts){
  return ncdirect_render_visual(n, frame, vopts);
}

int ncdirectf_geom(ncdirect* n, ncdirectf* frame,
                   const struct ncvisual_options* vopts, ncvgeom* geom){
  const struct blitset* bset;
  unsigned disppxy, disppxx, outy, outx;
  int placey, placex;
  return ncvisual_geom_inner(&n->tcache, frame, vopts, geom, &bset,
                             &disppxy, &disppxx, &outy, &outx,
                             &placey, &placex);
}

uint16_t ncdirect_supported_styles(const ncdirect* nc){
  return term_supported_styles(&nc->tcache);
}

char* ncdirect_detected_terminal(const ncdirect* nc){
  return termdesc_longterm(&nc->tcache);
}

const nccapabilities* ncdirect_capabilities(const ncdirect* n){
  return &n->tcache.caps;
}

bool ncdirect_canget_cursor(const ncdirect* n){
  if(get_escape(&n->tcache, ESCAPE_U7) == NULL){
    return false;
  }
  if(n->tcache.ttyfd < 0){
    return false;
  }
  return true;
}
