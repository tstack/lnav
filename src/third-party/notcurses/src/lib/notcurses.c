#include "linux.h"
#include "version.h"
#include <uniwidth.h>
#include "egcpool.h"
#include "internal.h"
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistr.h>
#include <locale.h>
#include <uniwbrk.h>
#include <inttypes.h>
#include "compat/compat.h"
#include "unixsig.h"

#define ESC "\x1b"
#define TABSTOP 8

void notcurses_version_components(int* major, int* minor, int* patch, int* tweak){
  *major = NOTCURSES_VERNUM_MAJOR;
  *minor = NOTCURSES_VERNUM_MINOR;
  *patch = NOTCURSES_VERNUM_PATCH;
  *tweak = atoi(NOTCURSES_VERSION_TWEAK);
}

int ncwidth(uint32_t ch, const char*encoding)
{
    return uc_width(ch, encoding);
}

int notcurses_enter_alternate_screen(notcurses* nc){
  if(nc->tcache.ttyfd < 0){
    return -1;
  }
  if(enter_alternate_screen(nc->tcache.ttyfd, nc->ttyfp, &nc->tcache, nc->flags & NCOPTION_DRAIN_INPUT)){
    return -1;
  }
  ncplane_set_scrolling(notcurses_stdplane(nc), false);
  return 0;
}

int notcurses_leave_alternate_screen(notcurses* nc){
  if(nc->tcache.ttyfd < 0){
    return -1;
  }
  if(leave_alternate_screen(nc->tcache.ttyfd, nc->ttyfp,
                            &nc->tcache, nc->flags & NCOPTION_DRAIN_INPUT)){
    return -1;
  }
  // move to the end of our output
  if(nc->rstate.logendy < 0){
    return 0;
  }
  ncplane_cursor_move_yx(notcurses_stdplane(nc), nc->rstate.logendy, nc->rstate.logendx);
  return 0;
}

// reset the current colors, styles, and palette. called on startup (to purge
// any preexisting styling) and shutdown (to not affect further programs).
int reset_term_attributes(const tinfo* ti, fbuf* f){
  int ret = 0;
  const char* esc;
  if((esc = get_escape(ti, ESCAPE_OP)) && fbuf_emit(f, esc)){
    ret = -1;
  }
  if((esc = get_escape(ti, ESCAPE_SGR0)) && fbuf_emit(f, esc)){
    ret = -1;
  }
  return ret;
}

// attempt to restore the palette. if XT{PUSH,POP}COLORS is supported, use
// XTPOPCOLORS. if we can program individual colors, and we read the palette,
// reload it from our initial capture. otherwise, use "oc" if available; this
// will blow away any preexisting palette in favor of the default. if we've
// never touched the palette, don't bother trying to restore it (unless we're
// using XTPOPCOLORS, since in that case we always used XTPUSHCOLORS).
int reset_term_palette(const tinfo* ti, fbuf* f, unsigned touchedpalette){
  int ret = 0;
  const char* esc;
  if((esc = get_escape(ti, ESCAPE_RESTORECOLORS))){
    loginfo("restoring palette via xtpopcolors");
    if(fbuf_emit(f, esc)){
      ret = -1;
    }
    return ret;
  }
  if(!touchedpalette){
    return 0;
  }
  if(ti->caps.can_change_colors && ti->maxpaletteread > -1){
    loginfo("restoring saved palette (%d)", ti->maxpaletteread + 1);
    esc = get_escape(ti, ESCAPE_INITC);
    for(int z = 0 ; z < ti->maxpaletteread ; ++z){
      unsigned r, g, b;
      ncchannel_rgb8(ti->originalpalette.chans[z], &r, &g, &b);
      // Need convert RGB values [0..256) to [0..1000], ugh
      r = r * 1000 / 255;
      g = g * 1000 / 255;
      b = b * 1000 / 255;
      if(fbuf_emit(f, tiparm(esc, z, r, g, b)) < 0){
        return -1;
      }
    }
  }else if((esc = get_escape(ti, ESCAPE_OC))){
    loginfo("resetting palette");
    if(fbuf_emit(f, esc)){
      ret = -1;
    }
  }else{
    logwarn("no method known to restore palette");
  }
  return ret;
}

// Do the minimum necessary stuff to restore the terminal, then return. This is
// the end of the line for fatal signal handlers. notcurses_stop() will go on
// to tear down and account for internal structures. note that we do lots of
// shit here that is unsafe within a signal handler =[ FIXME.
static int
notcurses_stop_minimal(void* vnc){
  notcurses* nc = vnc;
  int ret = 0;
  ret |= drop_signals(nc);
  // collect output into the memstream buffer, and then dump it directly using
  // blocking_write(), to avoid problems with unreliable fflush().
  fbuf* f = &nc->rstate.f;
  fbuf_reset(f);
  // be sure to write the restoration sequences *prior* to running rmcup, as
  // they apply to the screen (alternate or otherwise) we're actually using.
  const char* esc;
  ret |= reset_term_palette(&nc->tcache, f, nc->touched_palette);
  ret |= reset_term_attributes(&nc->tcache, f);
  if((esc = get_escape(&nc->tcache, ESCAPE_RMKX)) && fbuf_emit(f, esc)){
    ret = -1;
  }
  const char* cnorm = get_escape(&nc->tcache, ESCAPE_CNORM);
  if(cnorm && fbuf_emit(f, cnorm)){
    ret = -1;
  }
  if(fbuf_flush(f, nc->ttyfp)){
    ret = -1;
  }
  if(nc->tcache.ttyfd >= 0){
    ret |= notcurses_mice_disable(nc);
    if(nc->tcache.tpreserved){
      ret |= tcsetattr(nc->tcache.ttyfd, TCSAFLUSH, nc->tcache.tpreserved);
    }
    // don't use leave_alternate_screen() here; we need pop the keyboard
    // whether we're in regular or alternate screen, and we need it done
    // before returning to the regular screen if we're in the alternate. if
    // we drained input, we never sent a keyboard modifier; send none now.
    if(!(nc->flags & NCOPTION_DRAIN_INPUT)){
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
    if(nc->tcache.in_alt_screen){
      if((esc = get_escape(&nc->tcache, ESCAPE_RMCUP))){
        if(tty_emit(esc, nc->tcache.ttyfd)){
          ret = -1;
        }
        nc->tcache.in_alt_screen = 0;
      }
    }
  }
  logdebug("restored terminal, returning %d", ret);
  return ret;
}

static const char NOTCURSES_VERSION[] =
 NOTCURSES_VERSION_MAJOR "."
 NOTCURSES_VERSION_MINOR "."
 NOTCURSES_VERSION_PATCH;

const char* notcurses_version(void){
  return NOTCURSES_VERSION;
}

void* ncplane_set_userptr(ncplane* n, void* opaque){
  void* ret = n->userptr;
  n->userptr = opaque;
  return ret;
}

void* ncplane_userptr(ncplane* n){
  return n->userptr;
}

const void* ncplane_userptr_const(const ncplane* n){
  return n->userptr;
}

// is the cursor in an invalid position? it never should be, but it's probably
// better to make sure (it's cheap) than to read from/write to random crap.
static bool
cursor_invalid_p(const ncplane* n){
  if(n->y >= n->leny || n->x >= n->lenx){
    return true;
  }
  return false;
}

char* ncplane_at_cursor(const ncplane* n, uint16_t* stylemask, uint64_t* channels){
  return ncplane_at_yx(n, n->y, n->x, stylemask, channels);
}

char* ncplane_at_yx(const ncplane* n, int y, int x, uint16_t* stylemask, uint64_t* channels){
  if(y < 0){
    if(y != -1){
      logerror("invalid y: %d", y);
      return NULL;
    }
    y = n->y;
  }
  if(x < 0){
    if(x != -1){
      logerror("invalid x: %d", x);
      return NULL;
    }
    x = n->x;
  }
  if((unsigned)y >= n->leny || (unsigned)x >= n->lenx){
    logerror("invalid coordinates: %d/%d", y, x);
    return NULL;
  }
  if(n->sprite){
    if(stylemask){
      *stylemask = 0;
    }
    if(channels){
      *channels = 0;
    }
    return strdup(n->sprite->glyph.buf);
  }
  const nccell* yx = &n->fb[nfbcellidx(n, y, x)];
  // if we're the right side of a wide glyph, we return the main glyph
  if(nccell_wide_right_p(yx)){
    return ncplane_at_yx(n, y, x - 1, stylemask, channels);
  }
  char* ret = nccell_extract(n, yx, stylemask, channels);
  if(ret == NULL){
    return NULL;
  }
//fprintf(stderr, "GOT [%s]\n", ret);
  if(strcmp(ret, "") == 0){
    free(ret);
    ret = nccell_strdup(n, &n->basecell);
    if(ret == NULL){
      return NULL;
    }
    if(stylemask){
      *stylemask = n->basecell.stylemask;
    }
  }
  // FIXME load basecell channels if appropriate
  return ret;
}

int ncplane_at_cursor_cell(ncplane* n, nccell* c){
  return ncplane_at_yx_cell(n, n->y, n->x, c);
}

int ncplane_at_yx_cell(ncplane* n, int y, int x, nccell* c){
  if(n->sprite){
    logerror("invoked on a sprixel plane");
    return -1;
  }
  if(y < 0){
    if(y != -1){
      logerror("invalid y: %d", y);
      return -1;
    }
    y = n->y;
  }
  if(x < 0){
    if(x != -1){
      logerror("invalid x: %d", x);
      return -1;
    }
    x = n->x;
  }
  if((unsigned)y >= n->leny || (unsigned)x >= n->lenx){
    logerror("invalid coordinates: %d/%d", y, x);
    return -1;
  }
  nccell* targ = ncplane_cell_ref_yx(n, y, x);
  if(nccell_duplicate(n, c, targ)){
    return -1;
  }
  // FIXME take base cell into account where necessary!
  return strlen(nccell_extended_gcluster(n, targ));
}

void ncplane_set_cell_yx(ncplane* n, int y, int x, unsigned stylebits, uint64_t channels){
    if(n->sprite){
        logerror("invoked on a sprixel plane");
        return;
    }
    if(y < 0){
        if(y != -1){
            logerror("invalid y: %d", y);
            return;
        }
        y = n->y;
    }
    if(x < 0){
        if(x != -1){
            logerror("invalid x: %d", x);
            return;
        }
        x = n->x;
    }
    if((unsigned)y >= n->leny || (unsigned)x >= n->lenx){
        logerror("invalid coordinates: %d/%d", y, x);
        return;
    }
    nccell* targ = ncplane_cell_ref_yx(n, y, x);
    nccell_set_styles(targ, stylebits);
    nccell_set_channels(targ, channels);
}

void ncplane_on_styles_yx(ncplane* n, int y, int x, unsigned stylebits){
    if(n->sprite){
        logerror("invoked on a sprixel plane");
        return;
    }
    if(y < 0){
        if(y != -1){
            logerror("invalid y: %d", y);
            return;
        }
        y = n->y;
    }
    if(x < 0){
        if(x != -1){
            logerror("invalid x: %d", x);
            return;
        }
        x = n->x;
    }
    if((unsigned)y >= n->leny || (unsigned)x >= n->lenx){
        logerror("invalid coordinates: %d/%d", y, x);
        return;
    }
    nccell* targ = ncplane_cell_ref_yx(n, y, x);
    nccell_on_styles(targ, stylebits);
}

void ncplane_dim_yx(const ncplane* n, unsigned* rows, unsigned* cols){
  if(rows){
    *rows = n->leny;
  }
  if(cols){
    *cols = n->lenx;
  }
}

// anyone calling this needs ensure the ncplane's framebuffer is updated
// to reflect changes in geometry. also called at startup for standard plane.
// sets |cgeo_changed| high iff the cell geometry changed (will happen on a
//  resize, and on a font resize if the pixel geometry does not change).
// sets |pgeo_changed| high iff the cell-pixel geometry changed (will happen
//  on a font resize).
int update_term_dimensions(unsigned* rows, unsigned* cols, tinfo* tcache,
                           int margin_b, unsigned* cgeo_changed, unsigned* pgeo_changed){
  *pgeo_changed = 0;
  *cgeo_changed = 0;
  // if we're not a real tty, we presumably haven't changed geometry, return
  if(tcache->ttyfd < 0){
    if(rows){
      *rows = tcache->default_rows;
    }
    if(cols){
      *cols = tcache->default_cols;
    }
    tcache->cellpxy = 0;
    tcache->cellpxx = 0;
    return 0;
  }
  unsigned rowsafe, colsafe;
  if(rows == NULL){
    rows = &rowsafe;
    rowsafe = tcache->dimy;
  }
  if(cols == NULL){
    cols = &colsafe;
    colsafe = tcache->dimx;
  }
#ifndef __MINGW32__
  struct winsize ws;
  if(tiocgwinsz(tcache->ttyfd, &ws)){
    return -1;
  }
  *rows = ws.ws_row;
  *cols = ws.ws_col;
  unsigned cpixy;
  unsigned cpixx;
#ifdef __linux__
  if(tcache->linux_fb_fd >= 0){
    get_linux_fb_pixelgeom(tcache, &tcache->pixy, &tcache->pixx);
    cpixy = tcache->pixy / *rows;
    cpixx = tcache->pixx / *cols;
  }else{
#else
  {
#endif
    // we might have the pixel geometry from CSI14t, so don't override a valid
    // earlier response with 0s from the ioctl. we do want to fire off a fresh
    // CSI14t in this case, though FIXME.
    if(ws.ws_ypixel){
      tcache->pixy = ws.ws_ypixel;
      tcache->pixx = ws.ws_xpixel;
    }
    // update even if we didn't get values just now, because we need set
    // cellpx{y,x} up from an initial CSI14n, which set only pix{y,x}.
    cpixy = ws.ws_row ? tcache->pixy / ws.ws_row : 0;
    cpixx = ws.ws_col ? tcache->pixx / ws.ws_col : 0;
  }
  if(tcache->cellpxy != cpixy){
    tcache->cellpxy = cpixy;
    *pgeo_changed = 1;
  }
  if(tcache->cellpxx != cpixx){
    tcache->cellpxx = cpixx;
    *pgeo_changed = 1;
  }
  if(tcache->cellpxy == 0 || tcache->cellpxx == 0){
    tcache->pixel_draw = NULL; // disable support
  }
#else
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  // There is the buffer itself, which is similar in function to the scrollback
  // buffer in a Linux terminal, and there is the display window, which is the
  // visible view of that buffer. The addressable area (from a VT point of
  // view) spans the width of the buffer, but the height of the display window.
  //
  //  +--------------------------+      ^
  //  |                          |      |
  //  |                          |      |
  //  +-----+--------------+-----+ ^ w  | b
  //  |XXXXX|XXXXXXXXXXXXXX|XXXXX| | i  | u
  //  |XXXXX|XXXXXXXXXXXXXX|XXXXX| | n  | f
  //  |XXXXX|XXXXXXXXXXXXXX|XXXXX| | d  | f
  //  |XXXXX|XXXXXXXXXXXXXX|XXXXX| | o  | e
  //  +-----+--------------+-----+ v w  | r
  //  |                          |      |
  //  |                          |      |
  //  +--------------------------+      v
  //
  //      <--- window --->
  //
  //<--------- buffer --------->
  //
  // Because the buffer extends past the bottom of the display window, a user
  // can potentially scroll down beyond what would normally be thought of as the
  // end of the buffer. Because the buffer can be wider than the display
  // window, the user can scroll horizontally to view parts of the addressable
  // area that aren't currently visible.
  if(GetConsoleScreenBufferInfo(tcache->outhandle, &csbi)){
    *cols = csbi.dwSize.X;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }else{
    *rows = tcache->default_rows;
    *cols = tcache->default_cols;
  }
#endif
  if(tcache->dimy != *rows){
    tcache->dimy = *rows;
    *cgeo_changed = 1;
  }
  if(tcache->dimx != *cols){
    tcache->dimx = *cols;
    *cgeo_changed = 1;
  }
  if(tcache->sixel_maxy_pristine){
    int sixelrows = *rows - 1;
    // if the bottom margin is at least one row, we can draw into the last
    // row of our visible area. we must leave the true bottom row alone.
    if(margin_b){
      ++sixelrows;
    }
    tcache->sixel_maxy = sixelrows * tcache->cellpxy;
    if(tcache->sixel_maxy > tcache->sixel_maxy_pristine){
      tcache->sixel_maxy = tcache->sixel_maxy_pristine;
    }
  }
  return 0;
}

// destroy the sprixels of an ncpile (this will not hide the sprixels)
static void
free_sprixels(ncpile* n){
  while(n->sprixelcache){
    sprixel* tmp = n->sprixelcache->next;
    sprixel_free(n->sprixelcache);
    n->sprixelcache = tmp;
  }
}

// destroy an empty ncpile. only call with pilelock held.
static void
ncpile_destroy(ncpile* pile){
  if(pile){
    pile->prev->next = pile->next;
    pile->next->prev = pile->prev;
    free_sprixels(pile);
    free(pile->crender);
    free(pile);
  }
}

void free_plane(ncplane* p){
  if(p){
    // ncdirect fakes an ncplane with no ->pile
    if(ncplane_pile(p)){
      notcurses* nc = ncplane_notcurses(p);
      pthread_mutex_lock(&nc->stats.lock);
        --ncplane_notcurses(p)->stats.s.planes;
        ncplane_notcurses(p)->stats.s.fbbytes -= sizeof(*p->fb) * p->leny * p->lenx;
      pthread_mutex_unlock(&nc->stats.lock);
      if(p->above == NULL && p->below == NULL){
        pthread_mutex_lock(&nc->pilelock);
          ncpile_destroy(ncplane_pile(p));
        pthread_mutex_unlock(&nc->pilelock);
      }
    }
    if(p->widget){
      void* w = p->widget;
      void (*wdestruct)(void*) = p->wdestruct;
      p->widget = NULL;
      p->wdestruct = NULL;
      logdebug("calling widget destructor %p(%p)", wdestruct, w);
      wdestruct(w);
      logdebug("got the widget");
    }
    if(p->sprite){
      sprixel_hide(p->sprite);
    }
    destroy_tam(p);
    egcpool_dump(&p->pool);
    free(p->name);
    free(p->fb);
    free(p);
  }
}

// create a new ncpile. only call with pilelock held. the return value
// was assigned to n->pile.
__attribute__((malloc))
static ncpile*
make_ncpile(notcurses* nc, ncplane* n){
  ncpile* ret = malloc(sizeof(*ret));
  if(ret){
    ret->nc = nc;
    ret->top = n;
    ret->bottom = n;
    ret->roots = n;
    n->bprev = &ret->roots;
    if(nc->stdplane){ // stdplane (and thus stdpile) has already been created
      ret->prev = ncplane_pile(nc->stdplane)->prev;
      ncplane_pile(nc->stdplane)->prev->next = ret;
      ret->next = ncplane_pile(nc->stdplane);
      ncplane_pile(nc->stdplane)->prev = ret;
    }else{
      ret->prev = ret;
      ret->next = ret;
    }
    n->above = NULL;
    n->below = NULL;
    ret->dimy = nc->tcache.dimy;
    ret->dimx = nc->tcache.dimx;
    ret->cellpxy = nc->tcache.cellpxy;
    ret->cellpxx = nc->tcache.cellpxx;
    ret->crender = NULL;
    ret->crenderlen = 0;
    ret->sprixelcache = NULL;
    ret->scrolls = 0;
  }
  n->pile = ret;
  return ret;
}


static inline size_t  ncplane_sizeof_cellarray( unsigned rows, unsigned cols)
{
  size_t fbsize;
  size_t size;

  // (nat) This protects against size_t overflow and also checks
  // that dimensions are not zero en-passant. Why ?
  // Assume: size_t is 16 bit, unsigned is 8 bit (UINT_MAX: 0xFF)
  // nccell is sizeof( *p->fb): 0x10 (128 bit)
  //
  // 0xFF * 0xFF * 0x10 = 0xFE010, but overflows so: 0xE010
  // 0x3F * 0x3F * 0x10 = 0xf810 is the biggest square possible

  size = (size_t) cols * (size_t) rows;
  if( size < (size_t) cols || size < (size_t) rows)
    return 0;

  fbsize = sizeof( struct nccell) * size;
  if( fbsize <= size)
    return 0;

  return fbsize;
}
// create a new ncplane at the specified location (relative to the true screen,
// having origin at 0,0), having the specified size, and put it at the top of
// the planestack. its cursor starts at its origin; its style starts as null.
// a plane may exceed the boundaries of the screen, but must have positive
// size in both dimensions. bind the plane to 'n', which may be NULL to create
// a new pile. if bound to a plane instead, this plane moves when that plane
// moves, and coordinates to move to are relative to that plane.
// there are two denormalized case we also must handle, that of the "fake"
// isolated ncplane created by ncdirect for rendering visuals. in that case
// (and only in that case), nc is NULL (as is n). there's also creation of the
// initial standard plane, in which case nc is not NULL, but nc->stdplane *is*
// (as once more is n).
ncplane* ncplane_new_internal(notcurses* nc, ncplane* n,
                              const ncplane_options* nopts){
  if(nopts->flags >= (NCPLANE_OPTION_FIXED << 1u)){
    logwarn("provided unsupported flags %016" PRIx64, nopts->flags);
  }
  if(nopts->flags & NCPLANE_OPTION_HORALIGNED || nopts->flags & NCPLANE_OPTION_VERALIGNED){
    if(n == NULL){
      logerror("alignment requires a parent plane");
      return NULL;
    }
  }
  if(nopts->flags & NCPLANE_OPTION_MARGINALIZED){
    if(nopts->rows != 0 || nopts->cols != 0){
      logerror("geometry specified with margins (r=%u, c=%u)",
               nopts->rows, nopts->cols);
      return NULL;
    }
  }
  ncplane* p = malloc(sizeof(*p));
  if(p == NULL){
    return NULL;
  }
  p->scrolling = nopts->flags & NCPLANE_OPTION_VSCROLL;
  p->fixedbound = nopts->flags & NCPLANE_OPTION_FIXED;
  p->autogrow = nopts->flags & NCPLANE_OPTION_AUTOGROW;
  p->widget = NULL;
  p->wdestruct = NULL;
  if(nopts->flags & NCPLANE_OPTION_MARGINALIZED){
    p->margin_b = nopts->margin_b;
    p->margin_r = nopts->margin_r;
    if(n){ // use parent size
      p->leny = ncplane_dim_y(n);
      p->lenx = ncplane_dim_x(n);
    }else{ // use pile size
      notcurses_term_dim_yx(nc, &p->leny, &p->lenx);
    }
    if((p->leny -= p->margin_b) <= 0){
      p->leny = 1;
    }
    if((p->lenx -= p->margin_r) <= 0){
      p->lenx = 1;
    }
  }else{
    p->leny = nopts->rows;
    p->lenx = nopts->cols;
  }

  size_t fbsize = ncplane_sizeof_cellarray( p->leny, p->lenx);
  if( ! fbsize || (p->fb = calloc(1,fbsize)) == NULL){
    logerror("error allocating cellmatrix (r=%u, c=%u)",
             p->leny, p->lenx);
    free(p);
    return NULL;
  }
  p->x = p->y = 0;
  p->logrow = 0;
  p->sprite = NULL;
  p->blist = NULL;
  p->name = strdup(nopts->name ? nopts->name : "");
  p->halign = NCALIGN_UNALIGNED;
  p->valign = NCALIGN_UNALIGNED;
  p->tam = NULL;
  if(!n){ // new root/standard plane
    p->absy = nopts->y;
    p->absx = nopts->x;
    p->bnext = NULL;
    p->bprev = NULL;
    p->boundto = p;
  }else{ // bound to preexisting pile
    if(nopts->flags & NCPLANE_OPTION_HORALIGNED){
      p->absx = ncplane_halign(n, nopts->x, nopts->cols);
      p->halign = nopts->x;
    }else{
      p->absx = nopts->x;
    }
    p->absx += n->absx;
    if(nopts->flags & NCPLANE_OPTION_VERALIGNED){
      p->absy = ncplane_valign(n, nopts->y, nopts->rows);
      p->valign = nopts->y;
    }else{
      p->absy = nopts->y;
    }
    p->absy += n->absy;
    if( (p->bnext = n->blist) ){
      n->blist->bprev = &p->bnext;
    }
    p->bprev = &n->blist;
    *p->bprev = p;
    p->boundto = n;
  }
  // FIXME handle top/left margins
  p->resizecb = nopts->resizecb;
  p->stylemask = 0;
  p->channels = 0;
  egcpool_init(&p->pool);
  nccell_init(&p->basecell);
  p->userptr = nopts->userptr;
  if(nc == NULL){ // fake ncplane backing ncdirect object
    p->above = NULL;
    p->below = NULL;
    p->pile = NULL;
  }else{
    pthread_mutex_lock(&nc->pilelock);
      ncpile* pile = n ? ncplane_pile(n) : NULL;
      if( (p->pile = pile) ){ // existing pile
        p->above = NULL;
        if( (p->below = pile->top) ){ // always happens save initial plane
          pile->top->above = p;
        }else{
          pile->bottom = p;
        }
        pile->top = p;
      }else{ // new pile
        make_ncpile(nc, p);
      }
      pthread_mutex_lock(&nc->stats.lock);
        nc->stats.s.fbbytes += fbsize;
        ++nc->stats.s.planes;
      pthread_mutex_unlock(&nc->stats.lock);
    pthread_mutex_unlock(&nc->pilelock);
  }
  loginfo("created new %ux%u plane \"%s\" @ %dx%d",
          p->leny, p->lenx, p->name ? p->name : "", p->absy, p->absx);
  return p;
}

// create an ncplane of the specified dimensions, but do not yet place it in
// the z-buffer. clear out all cells. this is for a wholly new context.
static ncplane*
create_initial_ncplane(notcurses* nc, int dimy, int dimx){
  ncplane_options nopts = {
    .y = 0, .x = 0,
    .rows = dimy - (nc->margin_t + nc->margin_b),
    .cols = dimx - (nc->margin_l + nc->margin_r),
    .userptr = NULL,
    .name = "std",
    .resizecb = NULL,
    .flags = 0,
  };
  return nc->stdplane = ncplane_new_internal(nc, NULL, &nopts);
}

ncplane* notcurses_stdplane(notcurses* nc){
  return nc->stdplane;
}

const ncplane* notcurses_stdplane_const(const notcurses* nc){
  return nc->stdplane;
}

ncplane* ncplane_create(ncplane* n, const ncplane_options* nopts){
  return ncplane_new_internal(ncplane_notcurses(n), n, nopts);
}

ncplane* ncpile_create(notcurses* nc, const struct ncplane_options* nopts){
  return ncplane_new_internal(nc, NULL, nopts);
}

void ncplane_home(ncplane* n){
  n->x = 0;
  n->y = 0;
}

int ncplane_cursor_move_yx(ncplane* n, int y, int x){
  if(x < 0){
    if(x < -1){
      logerror("negative target x %d", x);
      return -1;
    }
  }else if((unsigned)x >= n->lenx){
    logerror("target x %d >= width %u", x, n->lenx);
    return -1;
  }else{
    n->x = x;
  }
  if(y < 0){
    if(y < -1){
      logerror("negative target y %d", y);
      return -1;
    }
  }else if((unsigned)y >= n->leny){
    logerror("target y %d >= height %u", y, n->leny);
    return -1;
  }else{
    n->y = y;
  }
  if(cursor_invalid_p(n)){
    logerror("invalid cursor following move (%d/%d)", n->y, n->x);
    return -1;
  }
  return 0;
}

int ncplane_cursor_move_rel(ncplane* n, int y, int x){
  if((int)n->y + y == -1){
    logerror("invalid target y -1");
    return -1;
  }else if((int)n->x + x == -1){
    logerror("invalid target x -1");
    return -1;
  }else return ncplane_cursor_move_yx(n, n->y + y, n->x + x);
}

ncplane* ncplane_dup(const ncplane* n, void* opaque){
  int dimy = n->leny;
  int dimx = n->lenx;
  const int placey = n->absy;
  const int placex = n->absx;
  struct ncplane_options nopts = {
    .y = placey,
    .x = placex,
    .rows = dimy,
    .cols = dimx,
    .userptr = opaque,
    .name = n->name,
    .resizecb = ncplane_resizecb(n),
    .flags = 0,
  };
  ncplane* newn = ncplane_create(n->boundto, &nopts);
  if(newn == NULL){
    return NULL;
  }
  // we don't duplicate sprites...though i'm unsure why not
  size_t fbsize = sizeof(*n->fb) * dimx * dimy;
  if(egcpool_dup(&newn->pool, &n->pool)){
    ncplane_destroy(newn);
    return NULL;
  }
  memmove(newn->fb, n->fb, fbsize);
  // don't use ncplane_cursor_move_yx() here; the cursor could be in an
  // invalid location, which will be disallowed, failing out.
  newn->y = n->y;
  newn->x = n->x;
  newn->halign = n->halign;
  newn->stylemask = ncplane_styles(n);
  newn->channels = ncplane_channels(n);
  // we dupd the egcpool, so just dup the goffset
  newn->basecell = n->basecell;
  return newn;
}

// call the resize callback for each bound child in turn. we only need to do
// the first generation; if they resize, they'll invoke
// ncplane_resize_internal(), leading to this function being called anew.
int resize_callbacks_children(ncplane* n){
  int ret = 0;
  for(struct ncplane* child = n->blist ; child ; child = child->bnext){
    if(child->resizecb){
      ret |= child->resizecb(child);
    }
  }
  return ret;
}

// basic consistency checks on resize requests
static int
ncplane_resize_internal_check(const ncplane* n, int keepy, int keepx,
                              unsigned keepleny, unsigned keeplenx,
                              int yoff, int xoff, unsigned ylen, unsigned xlen,
                              unsigned* rows, unsigned* cols){
  if(keepy < 0 || keepx < 0){ // can't start at negative origin
    logerror("can't retain negative offset %dx%d", keepy, keepx);
    return -1;
  }
  if((!keepleny && keeplenx) || (keepleny && !keeplenx)){ // both must be 0
    logerror("can't retain null dimension %ux%u", keepleny, keeplenx);
    return -1;
  }
  // can't be smaller than keep length
  if(ylen < keepleny){
    logerror("can't map in y dimension: %u < %u", ylen, keepleny);
    return -1;
  }
  if(xlen < keeplenx){
    logerror("can't map in x dimension: %u < %u", xlen, keeplenx);
    return -1;
  }
  if(ylen <= 0 || xlen <= 0){ // can't resize to trivial or negative size
    logerror("can't achieve meaningless size %ux%u", ylen, xlen);
    return -1;
  }
  ncplane_dim_yx(n, rows, cols);
  if(keepleny + keepy > *rows){
    logerror("can't keep %u@%d rows from %u", keepleny, keepy, *rows);
    return -1;
  }
  if(keeplenx + keepx > *cols){
    logerror("can't keep %u@%d cols from %u", keeplenx, keepx, *cols);
    return -1;
  }
  loginfo("%ux%u @ %d/%d → %u/%u @ %d/%d (want %ux%u@%d/%d)", *rows, *cols,
          n->absy, n->absx, ylen, xlen, n->absy + keepy + yoff, n->absx + keepx + xoff,
          keepleny, keeplenx, keepy, keepx);
  return 0;
}

// can be used on stdplane, unlike ncplane_resize() which prohibits it.
int ncplane_resize_internal(ncplane* n, int keepy, int keepx,
                            unsigned keepleny, unsigned keeplenx,
                            int yoff, int xoff,
                            unsigned ylen, unsigned xlen){
  unsigned rows, cols;
  if(ncplane_resize_internal_check(n, keepy, keepx, keepleny, keeplenx,
                                   yoff, xoff, ylen, xlen,
                                   &rows, &cols)){
    return -1;
  }
  if(n->absy == n->absy + keepy && n->absx == n->absx + keepx &&
      rows == ylen && cols == xlen){
    return 0;
  }
  notcurses* nc = ncplane_notcurses(n);
  if(n->sprite){
    sprixel_hide(n->sprite);
  }
  // we're good to resize. we'll need alloc up a new framebuffer, and copy in
  // those elements we're retaining, zeroing out the rest. alternatively, if
  // we've shrunk, we will be filling the new structure.
  int oldarea = rows * cols;
  int keptarea = keepleny * keeplenx;
  int newarea = ylen * xlen;
  size_t fbsize = sizeof(nccell) * newarea;
  nccell* fb;
  // there are two cases worth optimizing:
  //
  // * nothing is kept. we malloc() a new cellmatrix, dump the EGCpool in
  //    toto, and zero out the matrix. no copies, one memset.
  // * old and new x dimensions match, and we're keeping the full width.
  //    we release any cells we're about to lose, realloc() the cellmatrix,
  //    and zero out any new cells. so long as the realloc() doesn't move
  //    us, there are no copies, one memset, one iteration (since this is
  //    most often due to autogrowth by a single line, the likelihood that
  //    we remain where we are is pretty high).
  // * otherwise, we malloc() a new cellmatrix, zero out any new cells,
  //    copy over any reused cells, and release any lost cells. one
  //    gigantic iteration.
  // we might realloc instead of mallocing, in which case we NULL out
  // |preserved|. it must otherwise be free()d at the end.
  nccell* preserved = n->fb;
  if(cols == xlen && cols == keeplenx && keepleny && !keepy){
    // we need release the cells that we're losing, lest we leak EGCpool
    // memory. unfortunately, this means we mutate the plane on the error case.
    // any solution would involve copying them out first. we only do this if
    // we're keeping some, as we otherwise drop the EGCpool in toto.
    if(n->leny > keepleny){
      for(unsigned y = keepleny ; y < n->leny ; ++y){
        for(unsigned x = 0 ; x < n->lenx ; ++x){
          nccell_release(n, ncplane_cell_ref_yx(n, y, x));
        }
      }
    }
    if((fb = realloc(n->fb, fbsize)) == NULL){
      return -1;
    }
    preserved = NULL;
  }else{
    if((fb = malloc(fbsize)) == NULL){
      return -1;
    }
  }
  if(n->tam){
    loginfo("tam realloc to %d entries", newarea);
    // FIXME first, free any disposed auxiliary vectors!
    tament* tmptam = realloc(n->tam, sizeof(*tmptam) * newarea);
    if(tmptam == NULL){
      if(preserved){
        free(fb);
      }
      return -1;
    }
    n->tam = tmptam;
    if(newarea > oldarea){
      memset(n->tam + oldarea, 0, sizeof(*n->tam) * (newarea - oldarea));
    }
  }
  // update the cursor, if it would otherwise be off-plane
  if(n->y >= ylen){
    n->y = ylen - 1;
  }
  if(n->x >= xlen){
    n->x = xlen - 1;
  }
  pthread_mutex_lock(&nc->stats.lock);
    ncplane_notcurses(n)->stats.s.fbbytes -= sizeof(*fb) * (rows * cols);
    ncplane_notcurses(n)->stats.s.fbbytes += fbsize;
  pthread_mutex_unlock(&nc->stats.lock);
  const int oldabsy = n->absy;
  // go ahead and move. we can no longer fail at this point. but don't yet
  // resize, because n->len[xy] are used in fbcellidx() in the loop below. we
  // don't use ncplane_move_yx(), because we want to planebinding-invariant.
  n->absy += keepy + yoff;
  n->absx += keepx + xoff;
//fprintf(stderr, "absx: %d keepx: %d xoff: %d\n", n->absx, keepx, xoff);
  if(keptarea == 0){ // keep nothing, resize/move only.
    // if we're keeping nothing, dump the old egcspool. otherwise, we go ahead
    // and keep it. perhaps we ought compact it?
    memset(fb, 0, sizeof(*fb) * newarea);
    egcpool_dump(&n->pool);
  }else if(!preserved){
    // the x dimensions are equal, and we're keeping across the width. only the
    // y dimension changed. if we grew, we need zero out the new cells (if we
    // shrunk, we already released the old cells prior to the realloc).
    unsigned tozorch = (ylen - keepleny) * xlen * sizeof(*fb);
    if(tozorch){
      unsigned zorchoff = keepleny * xlen;
      memset(fb + zorchoff, 0, tozorch);
    }
  }else{
    // we currently have maxy rows of maxx cells each. we will be keeping rows
    // keepy..keepy + keepleny - 1 and columns keepx..keepx + keeplenx - 1.
    // anything else is zerod out. itery is the row we're writing *to*, and we
    // must write to each (and every cell in each).
    for(unsigned itery = 0 ; itery < ylen ; ++itery){
      int truey = itery + n->absy;
      int sourceoffy = truey - oldabsy;
//fprintf(stderr, "sourceoffy: %d keepy: %d ylen: %d\n", sourceoffy, keepy, ylen);
      // if we have nothing copied to this line, zero it out in one go
      if(sourceoffy < keepy || sourceoffy >= keepy + (int)keepleny){
//fprintf(stderr, "writing 0s to line %d of %d\n", itery, ylen);
        memset(fb + (itery * xlen), 0, sizeof(*fb) * xlen);
      }else{
        int copyoff = itery * xlen; // our target at any given time
        // we do have something to copy, and zero, one, or two regions to zero out
        unsigned copied = 0;
        if(xoff < 0){
          memset(fb + copyoff, 0, sizeof(*fb) * -xoff);
          copyoff += -xoff;
          copied += -xoff;
        }
        const int sourceidx = nfbcellidx(n, sourceoffy, keepx);
//fprintf(stderr, "copying line %d (%d) to %d (%d)\n", sourceoffy, sourceidx, copyoff / xlen, copyoff);
        memcpy(fb + copyoff, preserved + sourceidx, sizeof(*fb) * keeplenx);
        copyoff += keeplenx;
        copied += keeplenx;
        unsigned perline = xlen - copied;
        for(unsigned x = copyoff ; x < n->lenx ; ++x){
          nccell_release(n, ncplane_cell_ref_yx(n, sourceoffy, x));
        }
        memset(fb + copyoff, 0, sizeof(*fb) * perline);
      }
    }
  }
  n->fb = fb;
  n->lenx = xlen;
  n->leny = ylen;
  free(preserved);
  return resize_callbacks_children(n);
}

int ncplane_resize(ncplane* n, int keepy, int keepx,
                   unsigned keepleny, unsigned keeplenx,
                   int yoff, int xoff,
                   unsigned ylen, unsigned xlen){
  if(n == ncplane_notcurses(n)->stdplane){
//fprintf(stderr, "Can't resize standard plane\n");
    return -1;
  }
  return ncplane_resize_internal(n, keepy, keepx, keepleny, keeplenx,
                                 yoff, xoff, ylen, xlen);
}

int ncplane_destroy(ncplane* ncp){
  if(ncp == NULL){
    return 0;
  }
  if(ncplane_notcurses(ncp)->stdplane == ncp){
    logerror("won't destroy standard plane");
    return -1;
  }
//notcurses_debug(ncplane_notcurses(ncp), stderr);
  loginfo("destroying %dx%d plane \"%s\" @ %dx%d",
          ncp->leny, ncp->lenx, ncp->name ? ncp->name : NULL, ncp->absy, ncp->absx);
  int ret = 0;
  // dissolve our binding from behind (->bprev is either NULL, or its
  // predecessor on the bound list's ->bnext, or &ncp->boundto->blist)
  if(ncp->bprev){
    if( (*ncp->bprev = ncp->bnext) ){
      ncp->bnext->bprev = ncp->bprev;
    }
  }else if(ncp->bnext){
    //assert(ncp->boundto->blist == ncp);
    ncp->bnext->bprev = NULL;
  }
  // recursively reparent our children to the plane to which we are bound.
  // this will extract each one from the sibling list.
  struct ncplane* bound = ncp->blist;
  while(bound){
    struct ncplane* tmp = bound->bnext;
    ncplane* bindto = ((ncp == ncp->boundto) ? bound : ncp->boundto);
    if(ncplane_reparent_family(bound, bindto) == NULL){
      ret = -1;
    }
    bound = tmp;
  }
  // extract ourselves from the z-axis. do this *after* reparenting, in case
  // reparenting shifts up the z-axis somehow (though i don't think it can,
  // at least not within a pile?).
  if(ncp->above){
    ncp->above->below = ncp->below;
  }else{
    ncplane_pile(ncp)->top = ncp->below;
  }
  if(ncp->below){
    ncp->below->above = ncp->above;
  }else{
    ncplane_pile(ncp)->bottom = ncp->above;
  }
  free_plane(ncp);
  return ret;
}

int ncplane_destroy_family(ncplane *ncp){
  if(ncp == NULL){
    return 0;
  }
  if(ncplane_notcurses(ncp)->stdplane == ncp){
    logerror("won't destroy standard plane");
    return -1;
  }
  int ret = 0;
  while(ncp->blist){
    ret |= ncplane_destroy_family(ncp->blist);
  }
  ret |= ncplane_destroy(ncp);
  return ret;
}

// it's critical that we're using UTF-8 encoding if at all possible. since the
// client might not have called setlocale(2) (if they weren't reading the
// directions...), go ahead and try calling setlocale(LC_ALL, "") and then
// setlocale(LC_CTYPE, "C.UTF-8") ourselves *iff* we're not using UTF-8 *and*
// LANG is not explicitly set to "C" nor "POSIX". this still requires the user
// to have a proper locale generated and available on disk. either way, they're
// going to get a diagnostic (unless the user has explicitly configured a LANG
// of "C" or "POSIX"). recommended practice is for the client code to have
// called setlocale() themselves, and set the NCOPTION_INHIBIT_SETLOCALE flag.
// if that flag is set, we take the locale and encoding as we get them.
void init_lang(void){
#ifdef __MINGW32__
  if(setlocale(LC_ALL, ".UTF8") == NULL){
    logwarn("couldn't set LC_ALL to utf8");
  }
#endif
  const char* encoding = nl_langinfo(CODESET);
  if(encoding && encoding_is_utf8(encoding)){
    return; // already utf-8, great!
  }
  const char* lang = getenv("LANG");
  // if LANG was explicitly set to C/POSIX, life sucks, roll with it
  if(lang && (!strcmp(lang, "C") || !strcmp(lang, "POSIX"))){
    loginfo("LANG was explicitly set to %s, not changing locale", lang);
    return;
  }
#ifndef __MINGW32__
  if(setlocale(LC_ALL, "") == NULL){
    logwarn("setting locale based on LANG failed");
  }
#endif
  encoding = nl_langinfo(CODESET);
  if(encoding && encoding_is_utf8(encoding)){
    loginfo("set locale from LANG; client should call setlocale(2)!");
    return;
  }
  setlocale(LC_CTYPE, "C.UTF-8");
  encoding = nl_langinfo(CODESET);
  if(encoding && encoding_is_utf8(encoding)){
    loginfo("forced UTF-8 encoding; client should call setlocale(2)!");
    return;
  }
}

// initialize a recursive mutex lock in a way that works on both glibc + musl
static int
recursive_lock_init(pthread_mutex_t *lock){
#ifndef __GLIBC__
#define PTHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
#endif
  pthread_mutexattr_t attr;
  if(pthread_mutexattr_init(&attr)){
    return -1;
  }
  if(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP)){
    pthread_mutexattr_destroy(&attr);
    return -1;
  }
  if(pthread_mutex_init(lock, &attr)){
    pthread_mutexattr_destroy(&attr);
    return -1;
  }
  pthread_mutexattr_destroy(&attr);
  return 0;
#ifndef __GLIBC__
#undef PTHREAD_MUTEX_RECURSIVE_NP
#endif
}

ncpixelimpl_e notcurses_check_pixel_support(const notcurses* nc){
  if(nc->tcache.cellpxy == 0 || nc->tcache.cellpxx == 0){
    return NCPIXEL_NONE;
  }
  return nc->tcache.pixel_implementation;
}

// the earliest stage of initialization. logging does not yet work; all
// diagostics should be emitted with fprintf(stderr).
//  * validates |opts|, if not NULL
//  * creates and zeroes out the notcurses struct
//  * ensures that we're using ASCII or UTF8 and calls setlocale(3)
//  * checks the environment for NOTCURSES_LOGLEVEL and sets ret->loglevel
//  * writes TERM to the environment, if specified via opts->termtype
//
// iff we're using UTF8, |utf8| will be set to 1. it is otherwise set to 0.
__attribute__ ((nonnull (2))) static notcurses*
notcurses_early_init(const struct notcurses_options* opts, FILE* fp, unsigned* utf8){
  if(fwide(fp, 0) > 0){
    fprintf(stderr, "error: output stream is wide-oriented");
    return NULL;
  }
  notcurses* ret = malloc(sizeof(*ret));
  if(ret == NULL){
    return ret;
  }
  memset(ret, 0, sizeof(*ret));
  if(opts){
    if(opts->flags >= (NCOPTION_SCROLLING << 1u)){
      fprintf(stderr, "warning: unknown Notcurses options %016" PRIu64, opts->flags);
    }
    if(opts->termtype){
      if(putenv_term(opts->termtype)){
        free(ret);
        return NULL;
      }
    }
    ret->flags = opts->flags;
    ret->margin_t = opts->margin_t;
    ret->margin_b = opts->margin_b;
    ret->margin_l = opts->margin_l;
    ret->margin_r = opts->margin_r;
    ret->loglevel = opts->loglevel;
  }
  set_loglevel_from_env(&ret->loglevel);
  if(!(ret->flags & NCOPTION_INHIBIT_SETLOCALE)){
    init_lang();
  }
//fprintf(stderr, "getenv LC_ALL: %s LC_CTYPE: %s\n", getenv("LC_ALL"), getenv("LC_CTYPE"));
  const char* encoding = nl_langinfo(CODESET);
  if(encoding && encoding_is_utf8(encoding)){
    *utf8 = true;
  }else{
    *utf8 = false;
    if(encoding && (strcmp(encoding, "ANSI_X3.4-1968") &&
                          strcmp(encoding, "US-ASCII") &&
                          strcmp(encoding, "ASCII"))){
      fprintf(stderr, "encoding (\"%s\") was neither ANSI_X3.4-1968 nor UTF-8, refusing to start\n did you call setlocale()?\n",
              encoding ? encoding : "none found");
      free(ret);
      return NULL;
    }
  }
  ret->cursory = ret->cursorx = -1;
  reset_stats(&ret->stats.s);
  reset_stats(&ret->stashed_stats);
  ret->ttyfp = fp;
  egcpool_init(&ret->pool);
  if(ret->loglevel > NCLOGLEVEL_TRACE || ret->loglevel < NCLOGLEVEL_SILENT){
    fprintf(stderr, "invalid loglevel %d", ret->loglevel);
    free(ret);
    return NULL;
  }
  if(recursive_lock_init(&ret->pilelock)){
    fprintf(stderr, "couldn't initialize pile mutex");
    free(ret);
    return NULL;
  }
  if(pthread_mutex_init(&ret->stats.lock, NULL)){
    pthread_mutex_destroy(&ret->pilelock);
    free(ret);
    return NULL;
  }
  if(utf8){
    ncmetric_use_utf8();
  }
  return ret;
}

notcurses* notcurses_core_init(const notcurses_options* opts, FILE* outfp){
  if(outfp == NULL){
    outfp = stdout;
  }
  unsigned utf8;
  // ret comes out entirely zero-initialized
  notcurses* ret = notcurses_early_init(opts, outfp, &utf8);
  if(ret == NULL){
    return NULL;
  }
  // the fbuf is needed by notcurses_stop_minimal, so this must be done
  // before registering fatal signal handlers.
  if(fbuf_init(&ret->rstate.f)){
    pthread_mutex_destroy(&ret->pilelock);
    pthread_mutex_destroy(&ret->stats.lock);
    free(ret);
    return NULL;
  }
  if(setup_signals(ret, (ret->flags & NCOPTION_NO_QUIT_SIGHANDLERS),
                   (ret->flags & NCOPTION_NO_WINCH_SIGHANDLER),
                   notcurses_stop_minimal)){
    fbuf_free(&ret->rstate.f);
    pthread_mutex_destroy(&ret->pilelock);
    pthread_mutex_destroy(&ret->stats.lock);
    free(ret);
    return NULL;
  }
  // don't set loglevel until we've acquired the signal handler, lest we
  // change the loglevel out from under a running instance
  loglevel = ret->loglevel;
  ret->rstate.logendy = -1;
  ret->rstate.logendx = -1;
  ret->rstate.x = ret->rstate.y = -1;
  int fakecursory = ret->rstate.logendy;
  int fakecursorx = ret->rstate.logendx;
  int* cursory = ret->flags & NCOPTION_PRESERVE_CURSOR ?
                  &ret->rstate.logendy : &fakecursory;
  int* cursorx = ret->flags & NCOPTION_PRESERVE_CURSOR ?
                  &ret->rstate.logendx : &fakecursorx;
  if(interrogate_terminfo(&ret->tcache, ret->ttyfp, utf8,
                          ret->flags & NCOPTION_NO_ALTERNATE_SCREEN, 0,
                          ret->flags & NCOPTION_NO_FONT_CHANGES,
                          cursory, cursorx, &ret->stats,
                          ret->margin_l, ret->margin_t,
                          ret->margin_r, ret->margin_b,
                          ret->flags & NCOPTION_DRAIN_INPUT)){
    fbuf_free(&ret->rstate.f);
    pthread_mutex_destroy(&ret->pilelock);
    pthread_mutex_destroy(&ret->stats.lock);
    drop_signals(ret);
    free(ret);
    return NULL;
  }
  if(ret->tcache.maxpaletteread > -1){
    memcpy(ret->palette.chans, ret->tcache.originalpalette.chans,
           sizeof(*ret->palette.chans) * (ret->tcache.maxpaletteread + 1));
  }
  if((ret->flags & NCOPTION_PRESERVE_CURSOR) ||
      (!(ret->flags & NCOPTION_SUPPRESS_BANNERS))){
    // the u7 led the queries so that we would get a cursor position
    // unaffected by any query spill (unconsumed control sequences). move
    // us back to that location, in case there was any such spillage.
    if(*cursory < 0 || *cursorx < 0){
      unsigned cy, cx;
      if(locate_cursor(&ret->tcache, &cy, &cx)){
        logwarn("couldn't preserve cursor");
      }else{
        *cursory = cy;
        *cursorx = cx;
      }
    }
    if(*cursory >= 0 && *cursorx >= 0){
      if(goto_location(ret, &ret->rstate.f, *cursory, *cursorx, NULL)){
        goto err;
      }
    }
  }
  unsigned dimy, dimx, cgeo, pgeo; // latter two are don't-cares
  if(update_term_dimensions(&dimy, &dimx, &ret->tcache, ret->margin_b, &cgeo, &pgeo)){
    goto err;
  }
  if(ncvisual_init(ret->loglevel)){
    goto err;
  }
  ret->stdplane = NULL;
  if((ret->stdplane = create_initial_ncplane(ret, dimy, dimx)) == NULL){
    logpanic("couldn't create the initial plane (bad margins?)");
    goto err;
  }
  if(ret->flags & NCOPTION_SCROLLING){
    ncplane_set_scrolling(ret->stdplane, true);
  }
  reset_term_attributes(&ret->tcache, &ret->rstate.f);
  const char* cinvis = get_escape(&ret->tcache, ESCAPE_CIVIS);
  if(cinvis && fbuf_emit(&ret->rstate.f, cinvis) < 0){
    free_plane(ret->stdplane);
    goto err;
  }
  const char* pushcolors = get_escape(&ret->tcache, ESCAPE_SAVECOLORS);
  if(pushcolors && fbuf_emit(&ret->rstate.f, pushcolors)){
    free_plane(ret->stdplane);
    goto err;
  }
  if(fbuf_flush(&ret->rstate.f, ret->ttyfp) < 0){
    free_plane(ret->stdplane);
    goto err;
  }
  if(ret->rstate.logendy >= 0){ // if either is set, both are
    if(!(ret->flags & NCOPTION_SUPPRESS_BANNERS) && ret->tcache.ttyfd >= 0){
      unsigned uendy, uendx;
      if(locate_cursor(&ret->tcache, &uendy, &uendx)){
        free_plane(ret->stdplane);
        goto err;
      }
      ret->rstate.logendy = uendy;
      ret->rstate.logendx = uendx;
    }
    if(ret->flags & NCOPTION_PRESERVE_CURSOR){
      ncplane_cursor_move_yx(ret->stdplane, ret->rstate.logendy, ret->rstate.logendx);
    }
  }
  if(!(ret->flags & NCOPTION_NO_ALTERNATE_SCREEN)){
    // perform an explicit clear since the alternate screen was requested
    // (smcup *might* clear, but who knows? and it might not have been
    // available in any case).
    if(clear_and_home(ret, &ret->tcache, &ret->rstate.f)){
      goto err;
    }
    // no need to reestablish a preserved cursor -- that only affects the
    // standard plane, not the physical cursor that was just disrupted.
  }
  // the sprite clear ought take place within the alternate screen, if it's
  // being used.
  if(!(ret->flags & NCOPTION_NO_CLEAR_BITMAPS)){
    if(sprite_clear_all(&ret->tcache, &ret->rstate.f)){
      goto err;
    }
  }
  if(ret->rstate.f.used){
    if(fbuf_flush(&ret->rstate.f, ret->ttyfp) < 0){
      goto err;
    }
  }
  return ret;

err:
  logpanic("alas, you will not be going to space today.");
  notcurses_stop_minimal(ret);
  fbuf_free(&ret->rstate.f);
  if(ret->tcache.ttyfd >= 0 && ret->tcache.tpreserved){
    (void)tcsetattr(ret->tcache.ttyfd, TCSAFLUSH, ret->tcache.tpreserved);
    free(ret->tcache.tpreserved);
  }
  drop_signals(ret);
  del_curterm(cur_term);
  pthread_mutex_destroy(&ret->stats.lock);
  pthread_mutex_destroy(&ret->pilelock);
  free(ret);
  return NULL;
}

// updates *pile to point at (*pile)->next, frees all but standard pile/plane
static void
ncpile_drop(notcurses* nc, ncpile** pile){
  bool sawstdplane = false;
  ncpile* next = (*pile)->next;
  ncplane* p = (*pile)->top;
  while(p){
    ncplane* tmp = p->below;
    logdebug("killing plane %p, next is %p", p, tmp);
    if(nc->stdplane != p){
      free_plane(p);
    }else{
      sawstdplane = true;
    }
    p = tmp;
  }
  *pile = next;
  if(sawstdplane){
    ncplane_pile(nc->stdplane)->top = nc->stdplane;
    ncplane_pile(nc->stdplane)->bottom = nc->stdplane;
    nc->stdplane->above = nc->stdplane->below = NULL;
    nc->stdplane->blist = NULL;
  }
}

// drop all piles and all planes, save the standard plane and its pile
void notcurses_drop_planes(notcurses* nc){
  logdebug("we have some planes");
  pthread_mutex_lock(&nc->pilelock);
  ncpile* p = ncplane_pile(nc->stdplane);
  ncpile* p0 = p;
  do{
    ncpile_drop(nc, &p);
  }while(p0 != p);
  pthread_mutex_unlock(&nc->pilelock);
  logdebug("all planes dropped");
}

int notcurses_stop(notcurses* nc){
  logdebug("stopping notcurses");
//notcurses_debug(nc, stderr);
  int ret = 0;
  if(nc){
    ret |= notcurses_stop_minimal(nc);
    // if we were not using the alternate screen, our cursor's wherever we last
    // wrote. move it to the furthest place to which it advanced.
    if(!get_escape(&nc->tcache, ESCAPE_SMCUP)){
      fbuf_reset(&nc->rstate.f);
//fprintf(stderr, "CLOSING TO %d/%d\n", nc->rstate.logendy, nc->rstate.logendx);
      goto_location(nc, &nc->rstate.f, nc->rstate.logendy, nc->rstate.logendx, NULL);
//fprintf(stderr, "***"); fflush(stderr);
      fbuf_finalize(&nc->rstate.f, stdout);
    }
    if(nc->stdplane){
      notcurses_drop_planes(nc);
      free_plane(nc->stdplane);
    }
    if(nc->tcache.ttyfd >= 0){
      ret |= close(nc->tcache.ttyfd);
    }
    egcpool_dump(&nc->pool);
    free(nc->lastframe);
    free_terminfo_cache(&nc->tcache);
    // get any current stats loaded into stash_stats
    notcurses_stats_reset(nc, NULL);
    if(!(nc->flags & NCOPTION_SUPPRESS_BANNERS)){
      summarize_stats(nc);
    }
#ifndef __MINGW32__
    del_curterm(cur_term);
#endif
    ret |= pthread_mutex_destroy(&nc->stats.lock);
    ret |= pthread_mutex_destroy(&nc->pilelock);
    fbuf_free(&nc->rstate.f);
    free(nc);
  }
  return ret;
}

uint64_t ncplane_channels(const ncplane* n){
  return ncchannels_channels(n->channels);
}

void ncplane_set_channels(ncplane* n, uint64_t channels){
  ncchannels_set_channels(&n->channels, channels);
}

uint16_t ncplane_styles(const ncplane* n){
  return n->stylemask;
}

void ncplane_set_fg_default(ncplane* n){
  ncchannels_set_fg_default(&n->channels);
}

void ncplane_set_bg_default(ncplane* n){
  ncchannels_set_bg_default(&n->channels);
}

void ncplane_set_bg_rgb8_clipped(ncplane* n, int r, int g, int b){
  ncchannels_set_bg_rgb8_clipped(&n->channels, r, g, b);
}

int ncplane_set_bg_rgb8(ncplane* n, unsigned r, unsigned g, unsigned b){
  return ncchannels_set_bg_rgb8(&n->channels, r, g, b);
}

void ncplane_set_fg_rgb8_clipped(ncplane* n, int r, int g, int b){
  ncchannels_set_fg_rgb8_clipped(&n->channels, r, g, b);
}

int ncplane_set_fg_rgb8(ncplane* n, unsigned r, unsigned g, unsigned b){
  return ncchannels_set_fg_rgb8(&n->channels, r, g, b);
}

int ncplane_set_fg_rgb(ncplane* n, unsigned channel){
  return ncchannels_set_fg_rgb(&n->channels, channel);
}

uint64_t ncplane_set_bchannel(ncplane* n, uint32_t channel){
  return ncchannels_set_bchannel(&n->channels, channel);
}

uint64_t ncplane_set_fchannel(ncplane* n, uint32_t channel){
  return ncchannels_set_fchannel(&n->channels, channel);
}

int ncplane_set_bg_rgb(ncplane* n, unsigned channel){
  return ncchannels_set_bg_rgb(&n->channels, channel);
}

int ncplane_set_fg_alpha(ncplane* n, int alpha){
  return ncchannels_set_fg_alpha(&n->channels, alpha);
}

int ncplane_set_bg_alpha(ncplane *n, int alpha){
  return ncchannels_set_bg_alpha(&n->channels, alpha);
}

int ncplane_set_fg_palindex(ncplane* n, unsigned idx){
  return ncchannels_set_fg_palindex(&n->channels, idx);
}

int ncplane_set_bg_palindex(ncplane* n, unsigned idx){
  return ncchannels_set_bg_palindex(&n->channels, idx);
}

int ncplane_set_base_cell(ncplane* ncp, const nccell* c){
  if(nccell_wide_right_p(c)){
    return -1;
  }
  return nccell_duplicate(ncp, &ncp->basecell, c);
}

int ncplane_set_base(ncplane* ncp, const char* egc, uint16_t stylemask, uint64_t channels){
  return nccell_prime(ncp, &ncp->basecell, egc, stylemask, channels);
}

int ncplane_base(ncplane* ncp, nccell* c){
  return nccell_duplicate(ncp, c, &ncp->basecell);
}

const char* nccell_extended_gcluster(const ncplane* n, const nccell* c){
  if(cell_simple_p(c)){
    return (const char*)&c->gcluster;
  }
  return egcpool_extended_gcluster(&n->pool, c);
}

// 'n' ends up above 'above'
int ncplane_move_above(ncplane* restrict n, ncplane* restrict above){
  if(n == above){ // probably gets optimized out =/
    return -1;
  }
  ncpile* p = ncplane_pile(n);
  if(above == NULL){
    if(n->below){
      if( (n->below->above = n->above) ){
        n->above->below = n->below;
      }else{
        p->top = n->below;
      }
      n->below = NULL;
      if( (n->above = p->bottom) ){
        n->above->below = n;
      }
      p->bottom = n;
    }
    return 0;
  }
  if(n->below != above){
    if(p != ncplane_pile(above)){ // can't move among piles
      return -1;
    }
    // splice out 'n'
    if(n->below){
      n->below->above = n->above;
    }else{
      p->bottom = n->above;
    }
    if(n->above){
      n->above->below = n->below;
    }else{
      p->top = n->below;
    }
    if( (n->above = above->above) ){
      above->above->below = n;
    }else{
      p->top = n;
    }
    above->above = n;
    n->below = above;
  }
  return 0;
}

// 'n' ends up below 'below', or on top if 'below' == NULL
int ncplane_move_below(ncplane* restrict n, ncplane* restrict below){
  if(n == below){ // probably gets optimized out =/
    return -1;
  }
  ncpile* p = ncplane_pile(n);
  if(below == NULL){
    if(n->above){
      if( (n->above->below = n->below) ){
        n->below->above = n->above;
      }else{
        p->bottom = n->above;
      }
      n->above = NULL;
      if( (n->below = p->top) ){
        n->below->above = n;
      }
      p->top = n;
    }
    return 0;
  }
  if(n->above != below){
    if(p != ncplane_pile(below)){ // can't move among piles
      return -1;
    }
    if(n->below){
      n->below->above = n->above;
    }else{
      p->bottom = n->above;
    }
    if(n->above){
      n->above->below = n->below;
    }else{
      p->top = n->below;
    }
    if( (n->below = below->below) ){
      below->below->above = n;
    }else{
      p->bottom = n;
    }
    below->below = n;
    n->above = below;
  }
  return 0;
}

// if above is NULL, we're moving to the bottom
int ncplane_move_family_above(ncplane* restrict n, ncplane* restrict bpoint){
  ncplane* above = ncplane_above(n);
  ncplane* below = ncplane_below(n);
  if(ncplane_move_above(n, bpoint)){
    return -1;
  }
  // traverse the planes above n, until we hit NULL. do the planes above n
  // first, so that we know the topmost element of our new ensplicification.
  // at this point, n is the bottommost plane, and we're inserting above it.
  ncplane* targ = n;
  while(above && above != n){
    ncplane* tmp = ncplane_above(above);
    if(ncplane_descendant_p(above, n)){
      ncplane_move_above(above, targ);
      targ = above;
    }
    above = tmp;
  }
  // n remains the topmost plane, and we're inserting above it. we have to be
  // careful this time not to cross into any we moved below n.
  const ncplane* topmost = targ;
  targ = n;
  while(below && below != topmost){
    ncplane* tmp = ncplane_below(below);
    if(ncplane_descendant_p(below, n)){
      ncplane_move_below(below, targ);
      targ = below;
    }
    below = tmp;
  }
  return 0;
}

// if below is NULL, we're moving to the top
int ncplane_move_family_below(ncplane* restrict n, ncplane* restrict bpoint){
  ncplane* below = ncplane_below(n);
  ncplane* above = ncplane_above(n);
  if(ncplane_move_below(n, bpoint)){
    return -1;
  }
  // traverse the planes below n, until we hit NULL. do the planes below n
  // first, so that we know the bottommost element of our new ensplicification.
  // we're inserting below n...
  ncplane* targ = n;
  while(below && below != n){
    ncplane* tmp = ncplane_below(below);
    if(ncplane_descendant_p(below, n)){
      ncplane_move_below(below, targ);
      targ = below;
    }
    below = tmp;
  }
  // n remains the topmost plane, and we're inserting above it. we have to be
  // careful this time not to cross into any we moved below n.
  const ncplane* bottommost = targ;
  targ = n;
  while(above && above != bottommost){
    ncplane* tmp = ncplane_above(above);
    if(ncplane_descendant_p(above, n)){
      ncplane_move_above(above, targ);
      targ = above;
    }
    above = tmp;
  }
  return 0;
}

void ncplane_cursor_yx(const ncplane* n, unsigned* y, unsigned* x){
  if(y){
    *y = n->y;
  }
  if(x){
    *x = n->x;
  }
}

static inline void
nccell_obliterate(ncplane* n, nccell* c){
  nccell_release(n, c);
  nccell_init(c);
}

// increment y by 1 and rotate the framebuffer up one line. x moves to 0. any
// non-fixed bound planes move up 1 line if they intersect the plane.
void scroll_down(ncplane* n){
//fprintf(stderr, "pre-scroll: %d/%d %d/%d log: %d scrolling: %u\n", n->y, n->x, n->leny, n->lenx, n->logrow, n->scrolling);
  n->x = 0;
  if(n->y == n->leny - 1){
    // we're on the last line of the plane
    if(n->autogrow){
      ncplane_resize_simple(n, n->leny + 1, n->lenx);
      ncplane_cursor_move_yx(n, n->leny - 1, 0);
      return;
    }
    // we'll actually be scrolling material up and out, and making a new line.
    // if this is the standard plane, that means a "physical" scroll event is
    // called for.
    if(n == notcurses_stdplane(ncplane_notcurses(n))){
      ncplane_pile(n)->scrolls++;
    }
    n->logrow = (n->logrow + 1) % n->leny;
    nccell* row = n->fb + nfbcellidx(n, n->y, 0);
    for(unsigned clearx = 0 ; clearx < n->lenx ; ++clearx){
      nccell_release(n, &row[clearx]);
    }
    memset(row, 0, sizeof(*row) * n->lenx);
    for(struct ncplane* c = n->blist ; c ; c = c->bnext){
      if(!c->fixedbound){
        if(ncplanes_intersect_p(n, c)){
          ncplane_move_rel(c, -1, 0);
        }
      }
    }
  }else{
    ++n->y;
  }
}

int ncplane_scrollup(ncplane* n, int r){
  if(!ncplane_scrolling_p(n)){
    logerror("can't scroll %d on non-scrolling plane", r);
    return -1;
  }
  if(r < 0){
    logerror("can't scroll %d lines", r);
    return -1;
  }
  while(r-- > 0){
    scroll_down(n);
  }
  if(n == notcurses_stdplane(ncplane_notcurses(n))){
    notcurses_render(ncplane_notcurses(n));
  }
  return 0;
}

// Scroll |n| up until |child| is no longer hidden beneath it. Returns an
// error if |child| is not a child of |n|, or |n| is not scrolling, or |child|
// is fixed. Returns the number of scrolling events otherwise (might be 0).
int ncplane_scrollup_child(ncplane* n, const ncplane* child){
  if(!ncplane_descendant_p(child, n)){
    logerror("not a descendant of specified plane");
    return -1;
  }
  if(child->fixedbound){
    logerror("child plane is fixed");
    return -1;
  }
  int parend = ncplane_abs_y(n) + ncplane_dim_y(n) - 1; // where parent ends
  int chend = ncplane_abs_y(child) + ncplane_dim_y(child) - 1; // where child ends
  if(chend <= parend){
    return 0;
  }
  int r = chend - parend; // how many rows we need scroll parent
  ncplane_cursor_move_yx(n, ncplane_dim_y(n) - 1, 0);
  int ret = ncplane_scrollup(n, r);
  return ret;
}

int nccell_load(ncplane* n, nccell* c, const char* gcluster){
  int cols;
  int bytes = utf8_egc_len(gcluster, &cols);
  return pool_load_direct(&n->pool, c, gcluster, bytes, cols);
}

// where the magic happens. write the single EGC completely described by |egc|,
// occupying |cols| columns, to the ncplane |n| at the coordinate |y|, |x|. if
// either or both of |y|/|x| is -1, the current cursor location for that
// dimension will be used. if the glyph cannot fit on the current line, it is
// an error unless scrolling is enabled.
static int
ncplane_put(ncplane* n, int y, int x, const char* egc, int cols,
            uint16_t stylemask, uint64_t channels, int bytes){
  if(n->sprite){
    logerror("can't write [%s] to sprixelated plane", egc);
    return -1;
  }
  // reject any control character for output other than newline (and then only
  // on a scrolling plane) and tab.
  if(is_control_egc((const unsigned char*)egc, bytes)){
    if(*egc == '\n'){
      // if we're not scrolling, autogrow would be to the right (as opposed to
      // down), and thus it still wouldn't apply to the case of a newline.
      if(!n->scrolling){
        logerror("rejecting newline on non-scrolling plane");
        return -1;
      }
    }else if(*egc != '\t'){
      logerror("rejecting %dB control character", bytes);
      return -1;
    }
  }
  // check *before ncplane_cursor_move_yx()* whether we're past the end of the
  // line. if scrolling is enabled, move to the next line if so. if x or y are
  // specified, we must always try to print at exactly that location, and
  // there's no need to check the present location in that dimension.
  bool linesend = false;
  if(x < 0){
    // we checked x for all negatives, but only -1 is valid (our else clause is
    // predicated on a non-negative x).
    if(x == -1){
      if(n->x + cols - 1 >= n->lenx){
        linesend = true;
      }
    }
  }else{
    if((unsigned)x + cols - 1 >= n->lenx){
      linesend = true;
    }
  }
  bool scrolled = false;
  if(linesend){
    if(n->scrolling){
      scroll_down(n);
      scrolled = true;
    }else if(n->autogrow){
      ncplane_resize_simple(n, n->leny, n->lenx + cols);
    }else{
      logerror("target x %d [%.*s] > length %d", n->x, bytes, egc, n->lenx);
      return -1;
    }
  }
  // targets outside the plane will be rejected here.
  if(ncplane_cursor_move_yx(n, y, x)){
    return -1;
  }
  if(*egc == '\n'){
    scroll_down(n);
    scrolled = true;
  }
  // A wide character obliterates anything to its immediate right (and marks
  // that cell as wide). Any character placed atop one cell of a wide character
  // obliterates all cells. Note that a two-cell glyph can thus obliterate two
  // other two-cell glyphs, totalling four columns.
  nccell* targ = ncplane_cell_ref_yx(n, n->y, n->x);
  // we're always starting on the leftmost cell of our output glyph. check the
  // target, and find the leftmost cell of the glyph it will be displacing.
  // obliterate as we go along.
  int idx = n->x;
  nccell* lmc = targ;
  while(nccell_wide_right_p(lmc)){
    nccell_obliterate(n, &n->fb[nfbcellidx(n, n->y, idx)]);
    lmc = ncplane_cell_ref_yx(n, n->y, --idx);
  }
  // we're now on the leftmost cell of the target glyph.
  int twidth = nccell_cols(lmc);
  nccell_release(n, lmc);
  twidth -= n->x - idx;
  while(--twidth > 0){
    nccell_obliterate(n, &n->fb[nfbcellidx(n, n->y, n->x + twidth)]);
  }
  targ->stylemask = stylemask;
  targ->channels = channels;
  // tabs get replaced with spaces, up to the next tab stop. we always try to
  // write at least one. if there are no more tabstops on the current line, if
  // autogrowing to the right, extend as necessary. otherwise, if scrolling,
  // move to the next line. otherwise, simply fill any spaces we can. this has
  // already taken place by the time we get here, if it ought have happened.
  if(*egc == '\t'){
    cols = TABSTOP - (n->x % TABSTOP);
    if(n->x + 1 >= n->lenx){
      if(!n->scrolling && n->autogrow){
        ncplane_resize_simple(n, n->leny, n->lenx + (cols ? cols - 1 : TABSTOP - 1));
        // must refresh targ; resize invalidated it
        targ = ncplane_cell_ref_yx(n, n->y, n->x);
      }
    }
    if(cell_load_direct(n, targ, " ", bytes, 1) < 0){
      return -1;
    }
  }else{
    if(cell_load_direct(n, targ, egc, bytes, cols) < 0){
      return -1;
    }
  }
//fprintf(stderr, "%08x %016lx %c %d %d\n", targ->gcluster, targ->channels, nccell_double_wide_p(targ) ? 'D' : 'd', bytes, cols);
  if(*egc != '\n'){
    ++n->x;
    // if wide, set our right hand columns wide, and check for further damage
    for(int i = 1 ; i < cols ; ++i){
      nccell* candidate = &n->fb[nfbcellidx(n, n->y, n->x)];
      int off = nccell_cols(candidate);
      nccell_release(n, &n->fb[nfbcellidx(n, n->y, n->x)]);
      while(--off > 0){
        nccell_obliterate(n, &n->fb[nfbcellidx(n, n->y, n->x + off)]);
      }
      if(*egc != '\t'){
        candidate->channels = targ->channels;
        candidate->stylemask = targ->stylemask;
        candidate->width = targ->width;
      }else{
        if(cell_load_direct(n, candidate, " ", bytes, 1) < 0){
          return -1;
        }
      }
      ++n->x;
    }
  }
  if(scrolled){
    if(n == notcurses_stdplane(ncplane_notcurses(n))){
      notcurses_render(ncplane_notcurses(n));
    }
  }
  return cols;
}

int ncplane_putc_yx(ncplane* n, int y, int x, const nccell* c){
  const int cols = nccell_cols(c);
  // unfortunately, |c| comes from |n|. the act of writing |c| to |n| could
  // grow |n|'s egcpool, invalidating the reference represented by
  // nccell_extended_gcluster(). so we must copy and free it.
  char* egc = nccell_strdup(n, c);
  if(egc == NULL){
    logerror("couldn't duplicate cell");
    return -1;
  }
  int r = ncplane_put(n, y, x, egc, cols, c->stylemask, c->channels, strlen(egc));
  free(egc);
  return r;
}

int ncplane_putegc_yx(ncplane* n, int y, int x, const char* gclust, size_t* sbytes){
  int cols;
  int bytes = utf8_egc_len(gclust, &cols);
  if(bytes < 0){
    return -1;
  }
  if(sbytes){
    *sbytes = bytes;
  }
//fprintf(stderr, "glust: %s cols: %d wcs: %d\n", gclust, cols, bytes);
  return ncplane_put(n, y, x, gclust, cols, n->stylemask, n->channels, bytes);
}

int ncplane_putchar_stained(ncplane* n, char c){
  uint64_t channels = n->channels;
  uint16_t stylemask = n->stylemask;
  const nccell* targ = &n->fb[nfbcellidx(n, n->y, n->x)];
  n->channels = targ->channels;
  n->stylemask = targ->stylemask;
  int ret = ncplane_putchar(n, c);
  n->channels = channels;
  n->stylemask = stylemask;
  return ret;
}

int ncplane_putwegc_stained(ncplane* n, const wchar_t* gclust, size_t* sbytes){
  uint64_t channels = n->channels;
  uint16_t stylemask = n->stylemask;
  const nccell* targ = &n->fb[nfbcellidx(n, n->y, n->x)];
  n->channels = targ->channels;
  n->stylemask = targ->stylemask;
  int ret = ncplane_putwegc(n, gclust, sbytes);
  n->channels = channels;
  n->stylemask = stylemask;
  return ret;
}

int ncplane_putegc_stained(ncplane* n, const char* gclust, size_t* sbytes){
  uint64_t channels = n->channels;
  uint16_t stylemask = n->stylemask;
  const nccell* targ = &n->fb[nfbcellidx(n, n->y, n->x)];
  n->channels = targ->channels;
  n->stylemask = targ->stylemask;
  int ret = ncplane_putegc(n, gclust, sbytes);
  n->channels = channels;
  n->stylemask = stylemask;
  return ret;
}

int ncplane_cursor_at(const ncplane* n, nccell* c, char** gclust){
  if(n->y >= n->leny || n->x >= n->lenx){
    return -1;
  }
  const nccell* src = &n->fb[nfbcellidx(n, n->y, n->x)];
  memcpy(c, src, sizeof(*src));
  if(cell_simple_p(c)){
    *gclust = NULL;
  }else if((*gclust = strdup(nccell_extended_gcluster(n, src))) == NULL){
    return -1;
  }
  return 0;
}

uint16_t notcurses_supported_styles(const notcurses* nc){
  return term_supported_styles(&nc->tcache);
}

unsigned notcurses_palette_size(const notcurses* nc){
  return nc->tcache.caps.colors;
}

char* notcurses_detected_terminal(const notcurses* nc){
  return termdesc_longterm(&nc->tcache);
}

// conform to the specified stylebits
void ncplane_set_styles(ncplane* n, unsigned stylebits){
  n->stylemask = (stylebits & NCSTYLE_MASK);
}

// turn on any specified stylebits
void ncplane_on_styles(ncplane* n, unsigned stylebits){
  n->stylemask |= (stylebits & NCSTYLE_MASK);
}

// turn off any specified stylebits
void ncplane_off_styles(ncplane* n, unsigned stylebits){
  n->stylemask &= ~(stylebits & NCSTYLE_MASK);
}

// i hate the big allocation and two copies here, but eh what you gonna do?
// well, for one, we don't need the huge allocation FIXME
char* ncplane_vprintf_prep(const char* format, va_list ap){
  const size_t size = BUFSIZ; // healthy estimate, can embiggen below
  char* buf = malloc(size);
  if(buf == NULL){
    return NULL;
  }
  va_list vacopy;
  va_copy(vacopy, ap);
  int ret = vsnprintf(buf, size, format, ap);
  if(ret < 0){
    free(buf);
    va_end(vacopy);
    return NULL;
  }
  if((size_t)ret >= size){
    char* tmp = realloc(buf, ret + 1);
    if(tmp == NULL){
      free(buf);
      va_end(vacopy);
      return NULL;
    }
    buf = tmp;
    vsprintf(buf, format, vacopy);
  }
  va_end(vacopy);
  return buf;
}

int ncplane_vprintf_yx(ncplane* n, int y, int x, const char* format, va_list ap){
  char* r = ncplane_vprintf_prep(format, ap);
  if(r == NULL){
    return -1;
  }
  int ret = ncplane_putstr_yx(n, y, x, r);
  free(r);
  return ret;
}

int ncplane_vprintf_aligned(ncplane* n, int y, ncalign_e align,
                            const char* format, va_list ap){
  char* r = ncplane_vprintf_prep(format, ap);
  if(r == NULL){
    return -1;
  }
  int ret = ncplane_putstr_aligned(n, y, align, r);
  free(r);
  return ret;
}

int ncplane_vprintf_stained(struct ncplane* n, const char* format, va_list ap){
  char* r = ncplane_vprintf_prep(format, ap);
  if(r == NULL){
    return -1;
  }
  int ret = ncplane_putstr_stained(n, r);
  free(r);
  return ret;
}

int ncplane_putnstr_aligned(struct ncplane* n, int y, ncalign_e align, size_t s, const char* str){
  char* chopped = strndup(str, s);
  int ret = ncplane_putstr_aligned(n, y, align, chopped);
  free(chopped);
  return ret;
}

int ncplane_hline_interp(ncplane* n, const nccell* c, unsigned len,
                         uint64_t c1, uint64_t c2){
  if(len <= 0){
    logerror("passed invalid length %u", len);
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
  nccell dupc = NCCELL_TRIVIAL_INITIALIZER;
  if(nccell_duplicate(n, &dupc, c) < 0){
    return -1;
  }
  bool fgdef = false, bgdef = false;
  if((ncchannels_fg_default_p(c1) && ncchannels_fg_default_p(c2)) || ncchannels_fg_palindex_p(c1)){
    fgdef = true;
  }
  if((ncchannels_bg_default_p(c1) && ncchannels_bg_default_p(c2)) || ncchannels_bg_palindex_p(c1)){
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
      nccell_set_fg_rgb8(&dupc, r, g, b);
    }
    if(!bgdef){
      nccell_set_bg_rgb8(&dupc, br, bg, bb);
    }
    if(ncplane_putc(n, &dupc) <= 0){
      return -1;
    }
  }
  nccell_release(n, &dupc);
  return ret;
}

int ncplane_vline_interp(ncplane* n, const nccell* c, unsigned len,
                         uint64_t c1, uint64_t c2){
  if(len <= 0){
    logerror("passed invalid length %u", len);
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
  unsigned ypos, xpos;
  unsigned ret;
  ncplane_cursor_yx(n, &ypos, &xpos);
  nccell dupc = NCCELL_TRIVIAL_INITIALIZER;
  if(nccell_duplicate(n, &dupc, c) < 0){
    return -1;
  }
  bool fgdef = false, bgdef = false;
  if(ncchannels_fg_default_p(c1) && ncchannels_fg_default_p(c2)){
    fgdef = true;
  }
  if(ncchannels_bg_default_p(c1) && ncchannels_bg_default_p(c2)){
    bgdef = true;
  }
  for(ret = 0 ; ret < len ; ++ret){
    if(ncplane_cursor_move_yx(n, ypos + ret, xpos)){
      return -1;
    }
    r1 += deltr;
    g1 += deltg;
    b1 += deltb;
    br1 += deltbr;
    bg1 += deltbg;
    bb1 += deltbb;
    if(!fgdef){
      nccell_set_fg_rgb8(&dupc, r1, g1, b1);
    }
    if(!bgdef){
      nccell_set_bg_rgb8(&dupc, br1, bg1, bb1);
    }
    if(ncplane_putc(n, &dupc) <= 0){
      return -1;
    }
  }
  nccell_release(n, &dupc);
  return ret;
}

// we must have at least 2x2, or it's an error
int ncplane_box(ncplane* n, const nccell* ul, const nccell* ur,
                const nccell* ll, const nccell* lr, const nccell* hl,
                const nccell* vl, unsigned ystop, unsigned xstop,
                unsigned ctlword){
  unsigned yoff, xoff;
  ncplane_cursor_yx(n, &yoff, &xoff);
  // must be at least 2x2, with its upper-left corner at the current cursor
  if(ystop < yoff + 1){
    logerror("ystop (%u) insufficient for yoff (%d)", ystop, yoff);
    return -1;
  }
  if(xstop < xoff + 1){
    logerror("xstop (%u) insufficient for xoff (%d)", xstop, xoff);
    return -1;
  }
  unsigned ymax, xmax;
  ncplane_dim_yx(n, &ymax, &xmax);
  // must be within the ncplane
  if(xstop >= xmax || ystop >= ymax){
    logerror("boundary (%ux%u) beyond plane (%dx%d)", ystop, xstop, ymax, xmax);
    return -1;
  }
  unsigned edges;
  edges = !(ctlword & NCBOXMASK_TOP) + !(ctlword & NCBOXMASK_LEFT);
  if(edges >= box_corner_needs(ctlword)){
    if(ncplane_putc(n, ul) < 0){
      return -1;
    }
  }
  if(!(ctlword & NCBOXMASK_TOP)){ // draw top border, if called for
    if(xstop - xoff >= 2){
      if(ncplane_cursor_move_yx(n, yoff, xoff + 1)){
        return -1;
      }
      if(!(ctlword & NCBOXGRAD_TOP)){ // cell styling, hl
        if(ncplane_hline(n, hl, xstop - xoff - 1) < 0){
          return -1;
        }
      }else{ // gradient, ul -> ur
        if(ncplane_hline_interp(n, hl, xstop - xoff - 1, ul->channels, ur->channels) < 0){
          return -1;
        }
      }
    }
  }
  edges = !(ctlword & NCBOXMASK_TOP) + !(ctlword & NCBOXMASK_RIGHT);
  if(edges >= box_corner_needs(ctlword)){
    if(ncplane_cursor_move_yx(n, yoff, xstop)){
      return -1;
    }
    if(ncplane_putc(n, ur) < 0){
      return -1;
    }
  }
  ++yoff;
  // middle rows (vertical lines)
  if(yoff < ystop){
    if(!(ctlword & NCBOXMASK_LEFT)){
      if(ncplane_cursor_move_yx(n, yoff, xoff)){
        return -1;
      }
      if((ctlword & NCBOXGRAD_LEFT)){ // grad styling, ul->ll
        if(ncplane_vline_interp(n, vl, ystop - yoff, ul->channels, ll->channels) < 0){
          return -1;
        }
      }else{
        if(ncplane_vline(n, vl, ystop - yoff) < 0){
          return -1;
        }
      }
    }
    if(!(ctlword & NCBOXMASK_RIGHT)){
      if(ncplane_cursor_move_yx(n, yoff, xstop)){
        return -1;
      }
      if((ctlword & NCBOXGRAD_RIGHT)){ // grad styling, ur->lr
        if(ncplane_vline_interp(n, vl, ystop - yoff, ur->channels, lr->channels) < 0){
          return -1;
        }
      }else{
        if(ncplane_vline(n, vl, ystop - yoff) < 0){
          return -1;
        }
      }
    }
  }
  // bottom line
  yoff = ystop;
  edges = !(ctlword & NCBOXMASK_BOTTOM) + !(ctlword & NCBOXMASK_LEFT);
  if(edges >= box_corner_needs(ctlword)){
    if(ncplane_cursor_move_yx(n, yoff, xoff)){
      return -1;
    }
    if(ncplane_putc(n, ll) < 0){
      return -1;
    }
  }
  if(!(ctlword & NCBOXMASK_BOTTOM)){
    if(xstop - xoff >= 2){
      if(ncplane_cursor_move_yx(n, yoff, xoff + 1)){
        return -1;
      }
      if(!(ctlword & NCBOXGRAD_BOTTOM)){ // cell styling, hl
        if(ncplane_hline(n, hl, xstop - xoff - 1) < 0){
          return -1;
        }
      }else{
        if(ncplane_hline_interp(n, hl, xstop - xoff - 1, ll->channels, lr->channels) < 0){
          return -1;
        }
      }
    }
  }
  edges = !(ctlword & NCBOXMASK_BOTTOM) + !(ctlword & NCBOXMASK_RIGHT);
  if(edges >= box_corner_needs(ctlword)){
    if(ncplane_cursor_move_yx(n, yoff, xstop)){
      return -1;
    }
    if(ncplane_putc(n, lr) < 0){
      return -1;
    }
  }
  return 0;
}

// takes the head of a list of bound planes. performs a DFS on all planes bound
// to 'n', and all planes down-list from 'n', moving all *by* 'dy' and 'dx'.
static void
move_bound_planes(ncplane* n, int dy, int dx){
  while(n){
    if(n->sprite){
      sprixel_movefrom(n->sprite, n->absy, n->absx);
    }
    n->absy += dy;
    n->absx += dx;
    move_bound_planes(n->blist, dy, dx);
    n = n->bnext;
  }
}

int ncplane_move_yx(ncplane* n, int y, int x){
  if(n == ncplane_notcurses(n)->stdplane){
    return -1;
  }
  int dy, dx; // amount moved
  if(n->boundto == n){
    dy = y - n->absy;
    dx = x - n->absx;
  }else{
    dy = (n->boundto->absy + y) - n->absy;
    dx = (n->boundto->absx + x) - n->absx;
  }
  if(dy || dx){ // don't want to trigger sprixel_movefrom() if unneeded
    if(n->sprite){
      sprixel_movefrom(n->sprite, n->absy, n->absx);
    }
    n->absx += dx;
    n->absy += dy;
    move_bound_planes(n->blist, dy, dx);
  }
  return 0;
}

int ncplane_y(const ncplane* n){
  if(n->boundto == n){
    return n->absy;
  }
  return n->absy - n->boundto->absy;
}

int ncplane_x(const ncplane* n){
  if(n->boundto == n){
    return n->absx;
  }
  return n->absx - n->boundto->absx;
}

void ncplane_yx(const ncplane* n, int* y, int* x){
  if(y){
    *y = ncplane_y(n);
  }
  if(x){
    *x = ncplane_x(n);
  }
}

// special case of ncplane_erase_region()
void ncplane_erase(ncplane* n){
  loginfo("erasing %dx%d plane", n->leny, n->lenx);
  if(n->sprite){
    sprixel_hide(n->sprite);
    destroy_tam(n);
  }
  // we must preserve the background, but a pure nccell_duplicate() would be
  // wiped out by the egcpool_dump(). do a duplication (to get the stylemask
  // and channels), and then reload.
  char* egc = nccell_strdup(n, &n->basecell);
  memset(n->fb, 0, sizeof(*n->fb) * n->leny * n->lenx);
  egcpool_dump(&n->pool);
  egcpool_init(&n->pool);
  // we need to zero out the EGC before handing this off to nccell_load, but
  // we don't want to lose the channels/attributes, so explicit gcluster load.
  n->basecell.gcluster = 0;
  nccell_load(n, &n->basecell, egc);
  free(egc);
  n->y = n->x = 0;
}

int ncplane_erase_region(ncplane* n, int ystart, int xstart, int ylen, int xlen){
  if(ystart == -1){
    ystart = n->y;
  }
  if(xstart == -1){
    xstart = n->x;
  }
  if(ystart < 0 || xstart < 0){
    logerror("illegal start of erase (%d, %d)", ystart, xstart);
    return -1;
  }
  if(ystart >= (int)ncplane_dim_y(n) || xstart >= (int)ncplane_dim_x(n)){
    logerror("illegal start of erase (%d, %d)", ystart, xstart);
    return -1;
  }
  if(xlen < 0){
    if(xlen + 1 < -xstart){
      xlen = -xstart - 1;
    }
    xstart = xstart + xlen + 1;
    xlen = -xlen;
  }else if(xlen == 0){
    xstart = 0;
    xlen = ncplane_dim_x(n);
  }
  if(xlen > (int)ncplane_dim_x(n) || xstart + xlen > (int)ncplane_dim_x(n)){
    xlen = ncplane_dim_x(n) - xstart;
  }
  if(ylen < 0){
    if(ylen + 1 < -ystart){
      ylen = -ystart - 1;
    }
    ystart = ystart + ylen + 1;
    ylen = -ylen;
  }else if(ylen == 0){
    ystart = 0;
    ylen = ncplane_dim_y(n);
  }
  if(ylen > (int)ncplane_dim_y(n) || ystart + ylen > (int)ncplane_dim_y(n)){
    ylen = ncplane_dim_y(n) - ystart;
  }
  // special-case the full plane erasure, as it's powerfully optimized (O(1))
  if(ystart == 0 && xstart == 0 &&
      ylen == (int)ncplane_dim_y(n) && xlen == (int)ncplane_dim_x(n)){
    int tmpy = n->y; // preserve cursor location
    int tmpx = n->x;
    ncplane_erase(n);
    n->y = tmpy;
    n->x = tmpx;
    return 0;
  }
  loginfo("erasing %d/%d - %d/%d", ystart, xstart, ystart + ylen, xstart + xlen);
  for(int y = ystart ; y < ystart + ylen ; ++y){
    for(int x = xstart ; x < xstart + xlen ; ++x){
      nccell_release(n, &n->fb[nfbcellidx(n, y, x)]);
      nccell_init(&n->fb[nfbcellidx(n, y, x)]);
    }
  }
  return 0;
}

ncplane* ncpile_top(ncplane* n){
  return ncplane_pile(n)->top;
}

ncplane* ncpile_bottom(ncplane* n){
  return ncplane_pile(n)->bottom;
}

ncplane* ncplane_below(ncplane* n){
  return n->below;
}

ncplane* ncplane_above(ncplane* n){
  return n->above;
}

int notcurses_mice_enable(notcurses* n, unsigned eventmask){
  if(mouse_setup(&n->tcache, eventmask)){
    return -1;
  }
  return 0;
}

ncpalette* ncpalette_new(notcurses* nc){
  ncpalette* p = malloc(sizeof(*p));
  if(p){
    memcpy(p, &nc->palette, sizeof(*p));
  }
  return p;
}

int ncpalette_use(notcurses* nc, const ncpalette* p){
  int ret = -1;
  if(!notcurses_canchangecolor(nc)){
    return -1;
  }
  for(size_t z = 0 ; z < sizeof(p->chans) / sizeof(*p->chans) ; ++z){
    if(nc->palette.chans[z] != p->chans[z]){
      nc->palette.chans[z] = p->chans[z];
      nc->palette_damage[z] = true;
    }
  }
  ret = 0;
  return ret;
}

void ncpalette_free(ncpalette* p){
  free(p);
}

bool ncplane_translate_abs(const ncplane* n, int* restrict y, int* restrict x){
  ncplane_translate(ncplane_stdplane_const(n), n, y, x);
  if(y){
    if(*y < 0){
      return false;
    }
    unsigned yval = *y;
    if(yval >= n->leny){
      return false;
    }
  }
  if(x){
    if(*x < 0){
      return false;
    }
    unsigned xval = *x;
    if(xval >= n->lenx){
      return false;
    }
  }
  return true;
}

void ncplane_translate(const ncplane* src, const ncplane* dst,
                       int* restrict y, int* restrict x){
  if(dst == NULL){
    dst = ncplane_stdplane_const(src);
  }
  if(y){
    *y = src->absy - dst->absy + *y;
  }
  if(x){
    *x = src->absx - dst->absx + *x;
  }
}

notcurses* ncplane_notcurses(const ncplane* n){
  return ncplane_pile(n)->nc;
}

const notcurses* ncplane_notcurses_const(const ncplane* n){
  return ncplane_pile_const(n)->nc;
}

int ncplane_abs_y(const ncplane* n){
  return n->absy;
}

int ncplane_abs_x(const ncplane* n){
  return n->absx;
}

void ncplane_abs_yx(const ncplane* n, int* RESTRICT y, int* RESTRICT x){
  if(y){
    *y = ncplane_abs_y(n);
  }
  if(x){
    *x = ncplane_abs_x(n);
  }
}

ncplane* ncplane_parent(ncplane* n){
  return n->boundto;
}

const ncplane* ncplane_parent_const(const ncplane* n){
  return n->boundto;
}

int ncplane_set_name(ncplane* n, const char* name){
  char* copy = name ? strdup(name) : NULL;
  if(copy == NULL && name != NULL){
    return -1;
  }
  free(n->name);
  n->name = copy;
  return 0;
}

char* ncplane_name(const ncplane* n){
  return n->name ? strdup(n->name) : NULL;
}

void ncplane_set_resizecb(ncplane* n, int(*resizecb)(ncplane*)){
  n->resizecb = resizecb;
}

int (*ncplane_resizecb(const ncplane* n))(ncplane*){
  return n->resizecb;
}

int ncplane_resize_placewithin(ncplane* n){
  if(n->boundto == n){
    return 0;
  }
  int absy = ncplane_abs_y(n);
  int absx = ncplane_abs_x(n);
  int ret = 0;
  if(absy + ncplane_dim_y(n) > ncplane_dim_y(n->boundto)){
    const int dy = (absy + ncplane_dim_y(n)) - ncplane_dim_y(n->boundto);
    logdebug("moving up %d", dy);
    if(ncplane_move_rel(n, -dy, 0)){
      ret = -1;
    }
    absy = ncplane_abs_y(n);
  }
  if(absx + ncplane_dim_x(n) > ncplane_dim_x(n->boundto)){
    const int dx = ncplane_dim_x(n->boundto) - (absx + ncplane_dim_x(n));
    logdebug("moving left %d", dx);
    if(ncplane_move_rel(n, 0, dx)){
      ret = -1;
    }
    absx = ncplane_abs_x(n);
  }
  // this will prefer upper-left material if the child plane is larger than
  // the parent. we might want a smarter rule, one based on origin?
  if(absy < 0){
    logdebug("moving down %d", -absy);
    // we're at least partially above our parent
    if(ncplane_move_rel(n, -absy, 0)){
      ret = -1;
    }
  }
  if(absx < 0){
    logdebug("moving right %d", -absx);
    // we're at least partially to the left of our parent
    if(ncplane_move_rel(n, 0, -absx)){
      ret = -1;
    }
  }
  return ret;
}

int ncplane_resize_marginalized(ncplane* n){
  const ncplane* parent = ncplane_parent_const(n);
  // a marginalized plane cannot be larger than its oppressor plane =]
  unsigned maxy, maxx;
  if(parent == n){ // root plane, need to use pile size
    ncpile* p = ncplane_pile(n);
    maxy = p->dimy;
    maxx = p->dimx;
  }else{
    ncplane_dim_yx(parent, &maxy, &maxx);
  }
  if((maxy -= (n->margin_b + (n->absy - n->boundto->absy))) < 1){
    maxy = 1;
  }
  if((maxx -= (n->margin_r + (n->absx - n->boundto->absx))) < 1){
    maxx = 1;
  }
  unsigned oldy, oldx;
  ncplane_dim_yx(n, &oldy, &oldx); // current dimensions of 'n'
  unsigned keepleny = oldy > maxy ? maxy : oldy;
  unsigned keeplenx = oldx > maxx ? maxx : oldx;
  if(ncplane_resize_internal(n, 0, 0, keepleny, keeplenx, 0, 0, maxy, maxx)){
    return -1;
  }
  int targy = maxy - n->margin_b;
  int targx = maxx - n->margin_b;
  loginfo("marg %d/%d, pdim %d/%d, move %d/%d", n->margin_b, n->margin_r, maxy, maxx, targy, targx);
  return ncplane_move_yx(n, targy, targx);
}

int ncplane_resize_maximize(ncplane* n){
  const ncpile* pile = ncplane_pile(n); // FIXME should be taken against parent
  const unsigned rows = pile->dimy;
  const unsigned cols = pile->dimx;
  unsigned oldy, oldx;
  ncplane_dim_yx(n, &oldy, &oldx); // current dimensions of 'n'
  unsigned keepleny = oldy > rows ? rows : oldy;
  unsigned keeplenx = oldx > cols ? cols : oldx;
  return ncplane_resize_internal(n, 0, 0, keepleny, keeplenx, 0, 0, rows, cols);
}

int ncplane_resize_realign(ncplane* n){
  const ncplane* parent = ncplane_parent_const(n);
  if(parent == n){
    logerror("can't realign a root plane");
    return 0;
  }
  if(n->halign == NCALIGN_UNALIGNED && n->valign == NCALIGN_UNALIGNED){
    logerror("passed a non-aligned plane");
    return -1;
  }
  int xpos = ncplane_x(n);
  if(n->halign != NCALIGN_UNALIGNED){
    xpos = ncplane_halign(parent, n->halign, ncplane_dim_x(n));
  }
  int ypos = ncplane_y(n);
  if(n->valign != NCALIGN_UNALIGNED){
    ypos = ncplane_valign(parent, n->valign, ncplane_dim_y(n));
  }
  return ncplane_move_yx(n, ypos, xpos);
}

// The standard plane cannot be reparented; we return NULL in that case.
// If provided |newparent|==|n|, we are moving |n| to its own pile. If |n|
// is already bound to |newparent|, this is a no-op, and we return |n|.
// This is essentially a wrapper around ncplane_reparent_family() that first
// reparents any children to the parent of 'n', or makes them root planes if
// 'n' is a root plane.
ncplane* ncplane_reparent(ncplane* n, ncplane* newparent){
  const notcurses* nc = ncplane_notcurses_const(n);
  if(n == nc->stdplane){
    logerror("won't reparent standard plane");
    return NULL; // can't reparent standard plane
  }
  if(n->boundto == newparent){
    loginfo("won't reparent plane to itself");
    return n; // don't return error, just a no-op
  }
//notcurses_debug(ncplane_notcurses(n), stderr);
  if(n->blist){
    if(n->boundto == n){ // children become new root planes
      ncplane* lastlink;
      ncplane* child = n->blist;
      do{
        child->boundto = child;
        lastlink = child;
        child = child->bnext;
      }while(child); // n->blist != NULL -> lastlink != NULL
      if( (lastlink->bnext = ncplane_pile(n)->roots) ){
        lastlink->bnext->bprev = &lastlink->bnext;
      }
      n->blist->bprev = &ncplane_pile(n)->roots;
      ncplane_pile(n)->roots = n->blist;
    }else{ // children are rebound to current parent
      ncplane* lastlink;
      ncplane* child = n->blist;
      do{
        child->boundto = n->boundto;
        lastlink = child;
        child = child->bnext;
      }while(child); // n->blist != NULL -> lastlink != NULL
      if( (lastlink->bnext = n->boundto->blist) ){
        lastlink->bnext->bprev = &lastlink->bnext;
      }
      n->blist->bprev = &n->boundto->blist;
      n->boundto->blist = n->blist;
    }
    n->blist = NULL;
  }
  // FIXME would be nice to skip ncplane_descendant_p() on this call...:/
  return ncplane_reparent_family(n, newparent);
}

// unsplice self from the z-axis, and then unsplice all children, recursively.
// to be called before unbinding 'n' from old pile.
static void
unsplice_zaxis_recursive(ncplane* n){
  // might already have been unspliced, in which case ->above/->below are NULL
  if(ncplane_pile(n)->top == n){
    ncplane_pile(n)->top = n->below;
  }else if(n->above){
    n->above->below = n->below;
  }
  if(ncplane_pile(n)->bottom == n){
    ncplane_pile(n)->bottom = n->above;
  }else if(n->below){
    n->below->above = n->above;
  }
  for(ncplane* child = n->blist ; child ; child = child->bnext){
    unsplice_zaxis_recursive(child);
  }
  n->below = n->above = NULL;
}

// unsplice our sprixel from the pile's sprixellist, and then unsplice all
// children, recursively. call before unbinding. returns a doubly-linked
// list of any sprixels found.
static sprixel*
unsplice_sprixels_recursive(ncplane* n, sprixel* prev){
  sprixel* s = n->sprite;
  if(s){
    if(s->prev){
      s->prev->next = s->next;
    }else{
      ncplane_pile(n)->sprixelcache = s->next;
    }
    if(s->next){
      s->next->prev = s->prev;
    }
    if( (s->prev = prev) ){
      prev->next = s;
    }
    s->next = NULL;
    prev = s;
  }
  for(ncplane* child = n->blist ; child ; child = child->bnext){
    unsplice_sprixels_recursive(child, prev);
    while(prev && prev->next){ // FIXME lame
      prev = prev->next;
    }
  }
  return prev;
}

// recursively splice 'n' and children into the z-axis, above 'n->boundto'.
// handles 'n' == 'n->boundto'. to be called after binding 'n' into new pile.
static void
splice_zaxis_recursive(ncplane* n, ncpile* p, unsigned ocellpxy, unsigned ocellpxx,
                       unsigned ncellpxy, unsigned ncellpxx){
  n->pile = p;
  if(n != n->boundto){
    if((n->above = n->boundto->above) == NULL){
      n->pile->top = n;
    }else{
      n->boundto->above->below = n;
    }
    n->below = n->boundto;
    n->boundto->above = n;
  }
  if(n->sprite){
    if(ocellpxy != ncellpxy || ocellpxx != ncellpxx){
      sprixel_rescale(n->sprite, ncellpxy, ncellpxx);
      // FIXME do what on error?
    }
  }
  for(ncplane* child = n->blist ; child ; child = child->bnext){
    splice_zaxis_recursive(child, p, ocellpxy, ocellpxx, ncellpxy, ncellpxx);
  }
}

ncplane* ncplane_reparent_family(ncplane* n, ncplane* newparent){
  // ncplane_notcurses() goes through ncplane_pile(). since we're possibly
  // destroying piles below, get the notcurses reference early on.
  notcurses* nc = ncplane_notcurses(n);
  if(n == nc->stdplane){
    return NULL; // can't reparent standard plane
  }
  if(n->boundto == newparent){ // no-op
    return n;
  }
  if(ncplane_descendant_p(newparent, n)){
    return NULL;
  }
//notcurses_debug(ncplane_notcurses(n), stderr);
  if(n->bprev){ // extract from sibling list
    if( (*n->bprev = n->bnext) ){
      n->bnext->bprev = n->bprev;
    }
  }else if(n->bnext){
    n->bnext->bprev = NULL;
  }
  n->bprev = NULL;
  n->bnext = NULL;
  // if leaving a pile, extract n from the old zaxis, and also any sprixel
  sprixel* s = NULL;
  if(n == newparent || ncplane_pile(n) != ncplane_pile(newparent)){
    unsplice_zaxis_recursive(n);
    s = unsplice_sprixels_recursive(n, NULL);
  }
  const unsigned ocellpxy = ncplane_pile(n)->cellpxy;
  const unsigned ocellpxx = ncplane_pile(n)->cellpxx;
  n->boundto = newparent;
  if(n == n->boundto){ // we're a new root plane
    logdebug("reparenting new root plane %p", n);
    unsplice_zaxis_recursive(n);
    n->bnext = NULL;
    n->bprev = NULL;
    pthread_mutex_lock(&nc->pilelock);
    if(ncplane_pile(n)->top == NULL){ // did we just empty our pile?
      ncpile_destroy(ncplane_pile(n));
    }
    make_ncpile(nc, n);
    unsigned ncellpxy = ncplane_pile(n)->cellpxy;
    unsigned ncellpxx = ncplane_pile(n)->cellpxx;
    pthread_mutex_unlock(&nc->pilelock);
    if(ncplane_pile(n)){ // FIXME otherwise, we've got a problem...!
      splice_zaxis_recursive(n, ncplane_pile(n), ocellpxy, ocellpxx, ncellpxy, ncellpxx);
    }
  }else{ // establish ourselves as a sibling of new parent's children
    if( (n->bnext = newparent->blist) ){
      n->bnext->bprev = &n->bnext;
    }
    n->bprev = &newparent->blist;
    newparent->blist = n;
    // place it immediately above the new binding plane if crossing piles
    if(ncplane_pile(n) != ncplane_pile(n->boundto)){
      unsigned ncellpxy = ncplane_pile(n->boundto)->cellpxy;
      unsigned ncellpxx = ncplane_pile(n->boundto)->cellpxx;
      pthread_mutex_lock(&nc->pilelock);
      if(ncplane_pile(n)->top == NULL){ // did we just empty our pile?
        ncpile_destroy(ncplane_pile(n));
      }
      n->pile = ncplane_pile(n->boundto);
      pthread_mutex_unlock(&nc->pilelock);
      splice_zaxis_recursive(n, ncplane_pile(n), ocellpxy, ocellpxx, ncellpxy, ncellpxx);
    }
  }
  if(s){ // must be on new plane, with sprixels to donate
    sprixel* lame = s;
    while(lame->next){
      lame = lame->next;
    }
    if( (lame->next = n->pile->sprixelcache) ){
      n->pile->sprixelcache->prev = lame;
    }
    n->pile->sprixelcache = s;
  }
  return n;
}

bool ncplane_set_scrolling(ncplane* n, unsigned scrollp){
  bool old = n->scrolling;
  n->scrolling = scrollp;
  return old;
}

bool ncplane_scrolling_p(const ncplane* n){
  return n->scrolling;
}

bool ncplane_set_autogrow(ncplane* n, unsigned growp){
  if(n == notcurses_stdplane_const(ncplane_notcurses_const(n))){
    logerror("can't set the standard plane autogrow");
    return false;
  }
  bool old = n->autogrow;
  n->autogrow = growp;
  return old;
}

bool ncplane_autogrow_p(const ncplane* n){
  return n->autogrow;
}

// extract an integer, which must be non-negative, and followed by either a
// comma or a NUL terminator.
static int
lex_ulong(const char* op, unsigned* i, char** endptr){
  errno = 0;
  long l = strtol(op, endptr, 10);
  if(l < 0 || (l == LONG_MAX && errno == ERANGE) || (l > INT_MAX)){
    fprintf(stderr, "invalid margin: %s", op);
    return -1;
  }
  if((**endptr != ',' && **endptr) || *endptr == op){
    fprintf(stderr, "invalid margin: %s", op);
    return -1;
  }
  *i = l;
  return 0;
}

int notcurses_lex_scalemode(const char* op, ncscale_e* scalemode){
  if(strcasecmp(op, "stretch") == 0){
    *scalemode = NCSCALE_STRETCH;
  }else if(strcasecmp(op, "scalehi") == 0){
    *scalemode = NCSCALE_SCALE_HIRES;
  }else if(strcasecmp(op, "hires") == 0){
    *scalemode = NCSCALE_NONE_HIRES;
  }else if(strcasecmp(op, "scale") == 0){
    *scalemode = NCSCALE_SCALE;
  }else if(strcasecmp(op, "none") == 0){
    *scalemode = NCSCALE_NONE;
  }else{
    return -1;
  }
  return 0;
}

const char* notcurses_str_scalemode(ncscale_e scalemode){
  if(scalemode == NCSCALE_STRETCH){
    return "stretch";
  }else if(scalemode == NCSCALE_SCALE){
    return "scale";
  }else if(scalemode == NCSCALE_NONE){
    return "none";
  }else if(scalemode == NCSCALE_NONE_HIRES){
    return "hires";
  }else if(scalemode == NCSCALE_SCALE_HIRES){
    return "scalehi";
  }
  return NULL;
}

int notcurses_lex_margins(const char* op, notcurses_options* opts){
  char* eptr;
  if(lex_ulong(op, &opts->margin_t, &eptr)){
    return -1;
  }
  if(!*eptr){ // allow a single value to be specified for all four margins
    opts->margin_r = opts->margin_l = opts->margin_b = opts->margin_t;
    return 0;
  }
  op = ++eptr; // once here, we require four values
  if(lex_ulong(op, &opts->margin_r, &eptr) || !*eptr){
    return -1;
  }
  op = ++eptr;
  if(lex_ulong(op, &opts->margin_b, &eptr) || !*eptr){
    return -1;
  }
  op = ++eptr;
  if(lex_ulong(op, &opts->margin_l, &eptr) || *eptr){ // must end in NUL
    return -1;
  }
  return 0;
}

int notcurses_inputready_fd(notcurses* n){
  return inputready_fd(n->tcache.ictx);
}

int ncdirect_inputready_fd(ncdirect* n){
  return inputready_fd(n->tcache.ictx);
}

// FIXME speed this up, PoC
// given an egc, get its index in the blitter's EGC set
static int
get_blitter_egc_idx(const struct blitset* bset, const char* egc){
  wchar_t wc;
  mbstate_t mbs = {0};
  size_t sret = mbrtowc(&wc, egc, strlen(egc), &mbs);
  if(sret == (size_t)-1 || sret == (size_t)-2){
    return -1;
  }
  wchar_t* wptr = wcsrchr(bset->egcs, wc);
  if(wptr == NULL){
//fprintf(stderr, "FAILED TO FIND [%s] (%lc) in [%ls]\n", egc, wc, bset->egcs);
    return -1;
  }
//fprintf(stderr, "FOUND [%s] (%lc) in [%ls] (%zu)\n", egc, wc, bset->egcs, wptr - bset->egcs);
  return wptr - bset->egcs;
}

static bool
is_bg_p(int idx, int py, int px, int width){
  // bit increases to the right, and down
  const int bpos = py * width + px; // bit corresponding to pixel, 0..|egcs|-1
  const unsigned mask = 1u << bpos;
  if(idx & mask){
    return false;
  }
  return true;
}

static inline uint32_t*
ncplane_as_rgba_internal(const ncplane* nc, ncblitter_e blit,
                         int begy, int begx, unsigned leny, unsigned lenx,
                         unsigned* pxdimy, unsigned* pxdimx){
  const notcurses* ncur = ncplane_notcurses_const(nc);
  unsigned ystart, xstart;
  if(check_geometry_args(nc, begy, begx, &leny, &lenx, &ystart, &xstart)){
    return NULL;
  }
  if(blit == NCBLIT_PIXEL){ // FIXME extend this to support sprixels
    logerror("pixel blitter %d not yet supported", blit);
    return NULL;
  }
  if(blit == NCBLIT_DEFAULT){
    logerror("must specify exact blitter, not NCBLIT_DEFAULT");
    return NULL;
  }
  const struct blitset* bset = lookup_blitset(&ncur->tcache, blit, false);
  if(bset == NULL){
    logerror("blitter %d invalid in current environment", blit);
    return NULL;
  }
//fprintf(stderr, "ALLOCATING %u %d %d %p\n", 4u * lenx * leny * 2, leny, lenx, bset);
  if(pxdimy){
    *pxdimy = leny * bset->height;
  }
  if(pxdimx){
    *pxdimx = lenx * bset->width;
  }
  uint32_t* ret = malloc(sizeof(*ret) * lenx * bset->width * leny * bset->height);
//fprintf(stderr, "GEOM: %d/%d %d/%d ret: %p\n", bset->height, bset->width, *pxdimy, *pxdimx, ret);
  if(ret){
    for(unsigned y = ystart, targy = 0 ; y < ystart + leny ; ++y, targy += bset->height){
      for(unsigned x = xstart, targx = 0 ; x < xstart + lenx ; ++x, targx += bset->width){
        uint16_t stylemask;
        uint64_t channels;
        char* c = ncplane_at_yx(nc, y, x, &stylemask, &channels);
        if(c == NULL){
          free(ret);
          return NULL;
        }
        int idx = get_blitter_egc_idx(bset, c);
        if(idx < 0){
          free(ret);
          free(c);
          return NULL;
        }
        unsigned fr, fg, fb, br, bg, bb, fa, ba;
        ncchannels_fg_rgb8(channels, &fr, &fb, &fg);
        fa = ncchannels_fg_alpha(channels);
        ncchannels_bg_rgb8(channels, &br, &bb, &bg);
        ba = ncchannels_bg_alpha(channels);
        // handle each destination pixel from this cell
        for(unsigned py = 0 ; py < bset->height ; ++py){
          for(unsigned px = 0 ; px < bset->width ; ++px){
            uint32_t* p = &ret[(targy + py) * (lenx * bset->width) + (targx + px)];
            bool background = is_bg_p(idx, py, px, bset->width);
            if(background){
              if(ba){
                *p = 0;
              }else{
                ncpixel_set_a(p, 0xff);
                ncpixel_set_r(p, br);
                ncpixel_set_g(p, bb);
                ncpixel_set_b(p, bg);
              }
            }else{
              if(fa){
                *p = 0;
              }else{
                ncpixel_set_a(p, 0xff);
                ncpixel_set_r(p, fr);
                ncpixel_set_g(p, fb);
                ncpixel_set_b(p, fg);
              }
            }
          }
        }
        free(c);
      }
    }
  }
  return ret;
}

uint32_t* ncplane_as_rgba(const ncplane* nc, ncblitter_e blit,
                          int begy, int begx, unsigned leny,
                          unsigned lenx, unsigned* pxdimy, unsigned* pxdimx){
  unsigned px, py;
  if(!pxdimy){
    pxdimy = &py;
  }
  if(!pxdimx){
    pxdimx = &px;
  }
  return ncplane_as_rgba_internal(nc, blit, begy, begx, leny, lenx, pxdimy, pxdimx);
}

// return a heap-allocated copy of the contents
char* ncplane_contents(ncplane* nc, int begy, int begx, unsigned leny, unsigned lenx){
  unsigned ystart, xstart;
  if(check_geometry_args(nc, begy, begx, &leny, &lenx, &ystart, &xstart)){
    return NULL;
  }
  size_t retlen = 1;
  char* ret = malloc(retlen);
  if(ret){
    for(unsigned y = ystart, targy = 0 ; y < ystart + leny ; ++y, targy += 2){
      for(unsigned x = xstart, targx = 0 ; x < xstart + lenx ; ++x, ++targx){
        nccell ncl = NCCELL_TRIVIAL_INITIALIZER;
        // we need ncplane_at_yx_cell() here instead of ncplane_at_yx(),
        // because we should only have one copy of each wide EGC.
        int clen;
        if((clen = ncplane_at_yx_cell(nc, y, x, &ncl)) < 0){
          free(ret);
          return NULL;
        }
        const char* c = nccell_extended_gcluster(nc, &ncl);
        if(clen){
          char* tmp = realloc(ret, retlen + clen);
          if(!tmp){
            free(ret);
            return NULL;
          }
          ret = tmp;
          memcpy(ret + retlen - 1, c, clen);
          retlen += clen;
        }
      }
    }
    ret[retlen - 1] = '\0';
  }
  return ret;
}

// find the center coordinate of a plane, preferring the top/left in the
// case of an even number of rows/columns (in such a case, there will be one
// more cell to the bottom/right of the center than the top/left). the
// center is then modified relative to the plane's origin.
void ncplane_center_abs(const ncplane* n, int* RESTRICT y, int* RESTRICT x){
  ncplane_center(n, y, x);
  if(y){
    *y += n->absy;
  }
  if(x){
    *x += n->absx;
  }
}

int ncplane_putwstr_stained(ncplane* n, const wchar_t* gclustarr){
  mbstate_t ps = {0};
  const wchar_t** wset = &gclustarr;
  size_t mbytes = wcsrtombs(NULL, wset, 0, &ps);
  if(mbytes == (size_t)-1){
    logerror("error converting wide string");
    return -1;
  }
  ++mbytes;
  char* mbstr = malloc(mbytes);
  if(mbstr == NULL){
    return -1;
  }
  size_t s = wcsrtombs(mbstr, wset, mbytes, &ps);
  if(s == (size_t)-1){
    free(mbstr);
    return -1;
  }
  int r = ncplane_putstr_stained(n, mbstr);
  free(mbstr);
  return r;
}

int notcurses_ucs32_to_utf8(const uint32_t* ucs32, unsigned ucs32count,
                            unsigned char* resultbuf, size_t buflen){
  if(u32_to_u8(ucs32, ucs32count, resultbuf, &buflen) == NULL){
    return -1;
  }
  return buflen;
}

int ncstrwidth(const char* egcs, int* validbytes, int* validwidth){
  int cols;
  if(validwidth == NULL){
    validwidth = &cols;
  }
  *validwidth = 0;
  int bytes;
  if(validbytes == NULL){
    validbytes = &bytes;
  }
  *validbytes = 0;
  do{
    int thesecols, thesebytes;
    thesebytes = utf8_egc_len(egcs, &thesecols);
    if(thesebytes < 0){
      return -1;
    }
    egcs += thesebytes;
    *validbytes += thesebytes;
    *validwidth += thesecols;
  }while(*egcs);
  return *validwidth;
}

void ncplane_pixel_geom(const ncplane* n,
                        unsigned* RESTRICT pxy, unsigned* RESTRICT pxx,
                        unsigned* RESTRICT celldimy, unsigned* RESTRICT celldimx,
                        unsigned* RESTRICT maxbmapy, unsigned* RESTRICT maxbmapx){
  const notcurses* nc = ncplane_notcurses_const(n);
  const ncpile* p = ncplane_pile_const(n);
  if(celldimy){
    *celldimy = p->cellpxy;
  }
  if(celldimx){
    *celldimx = p->cellpxx;
  }
  if(pxy){
    *pxy = p->cellpxy * ncplane_dim_y(n);
  }
  if(pxx){
    *pxx = p->cellpxx * ncplane_dim_x(n);
  }
  if(notcurses_check_pixel_support(nc) > 0){
    if(maxbmapy){
      *maxbmapy = p->cellpxy * ncplane_dim_y(n);
      if(*maxbmapy > nc->tcache.sixel_maxy && nc->tcache.sixel_maxy){
        *maxbmapy = nc->tcache.sixel_maxy;
      }
    }
    if(maxbmapx){
      *maxbmapx = p->cellpxx * ncplane_dim_x(n);
      if(*maxbmapx > nc->tcache.sixel_maxx && nc->tcache.sixel_maxx){
        *maxbmapx = nc->tcache.sixel_maxx;
      }
    }
  }else{
    if(maxbmapy){
      *maxbmapy = 0;
    }
    if(maxbmapx){
      *maxbmapx = 0;
    }
  }
}

const nccapabilities* notcurses_capabilities(const notcurses* n){
  return &n->tcache.caps;
}
