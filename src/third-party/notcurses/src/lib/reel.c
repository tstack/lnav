#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

typedef enum {
  DIRECTION_UP,
  DIRECTION_DOWN,
} direction_e; // current direction of travel

// A UNIFIED THEORY OF NCREELS
// (which are more complex than they may seem)
//
// We only redraw when ncreel_redraw() is explicitly called (and at creation).
// Redrawing consists of arranging the tablets, and calling the user's
// callbacks to fill them in. In general, drawing a tablet involves creating a
// plane having all available size, calling the callback, and then trimming the
// plane if it was not filled.
//
// Things which can happen between ncreel_redraw() calls:
//
//  * new focused tablet added (only when no tablets exist)
//  * new unfocused tablet added
//  * tablet removed (may be the focused tablet)
//  * tablets may grow or shrink
//  * focus can change due to new selection
//
// Any number and mix of these events can occur between draws.
//
// First rule: there must not be 2+ consecutive lines of blank space if there is
//             data which could be presented there (always fill the reel).
// Second rule: if there must be 2+ consecutive lines of blank space, they must
//             all be at the bottom of the reel (connect and anchor the reel).
// Third rule: the focused tablet gets all the space it can use.
// Fourth rule: thou shalt never wrap a tablet [across a border]
// Fifth rule: the focused tablet should remain where it is across redraws,
//             except as necessary to accommodate the prior rules.
//
// At any ncreel_redraw(), you can make three types of moves:
//
//  - i. moving up and replacing the topmost tablet (spinning operation)
//  - ii. moving down and replacing the bottommost tablet (spinning operation)
//  - iii. don't move / move otherwise (no necessary spin)
//
// The first two are simple -- draw the focused tablet next to the appropriate
// border of the reel, and then draw what we can in the other direction until
// running out of space (and then shift up if there is more than one line of
// gap at the top, or if we were moving up from the topmost tablet). This can
// be done independently of all other tablets; it is immaterial if some were
// removed, added, etc. We can detect these two cases thus:
//
//  - store a direction_e, written to by ncreel_next(), ncreel_prev(), and
//     ncreel_del() (when the focused tablet is deleted), defaulting to DOWN.
//  - store a pointer to the "visibly focused" tablet, written and read only by
//     ncreel_redraw(). this identifies the focused tablet upon last redraw. if
//     the visibly-focused tablet is removed, it instead takes the next tablet,
//     iff that tablet is visible. it otherwise takes the prev tablet, iff that
//     tablet is visible. it otherwise takes NULL.
//  - with these, in ncreel_redraw(), we have:
//    - case i iff the last direction was UP, and either the focused tablet is
//       not visible, or below the visibly-focused tablet, or there is no
//       visibly-focused tablet.
//    - case ii iff the last direction was DOWN, and either the focused tablet
//       is not visible, or above the visibly-focused tablet, or there is no
//       visibly-focused tablet.
//
// We otherwise have case iii. The focused tablet must be on-screen (if it was
// off-screen, we matched one of case i or case ii). We want to draw it as near
// to its current position as possible, subject to the first four Rules.
//
// ncreel_redraw() thus starts by determining the case. This must be done
// before any changes are made to the arrangement. It then clears the reel.
// The focused tablet is drawn as close to its desired line as possible. For
// case i, then draw the tablets below the focused tablet. For case ii, then
// draw the tablets above the focused tablet (and move them up, if necessary).
// Both of these cases are then handled.
//
// For case iii, see below...
//
// On tablet remove:
//  * destroy plane, remove from list
//  * if tablet was focused, change focus, update travel direction
//  * if tablet was visibly focused, change visible focus
// On tablet add:
//  * add to list (do not create plane)
//  * if no tablet existed, change focus to new tablet
// On focus change:
//  * change focus, update travel direction
// On redraw:
//  * if no tablets, we're done (deleted planes are already gone)
//  * resize focused tablet to maximum, preserving nothing
//  * call back for focused tablet redraw
//  * shrink focused tablet if applicable
//  * place focused tablet according to:
//    * the focused tablet should be wholly visible, or if not, use all space
//    * the focused tablet should be as close to its old position as possible.
//    * if focused tablet was not wholly on screen, it goes to the side
//       corresponding to the direction of movement.
//  * if out of space or tablets, we're done
//  FIXME *maybe* just draw up followed by down, rather than direction of travel?
//  * walk the list in the direction of travel, foc->focw
//    * designate tablet against the walk as 'focagainst', might be NULL
//    * if focagainst || focagainst, focw only through edge, otherwise
//    * focw can be as large as all remaining space
//    * if there is space, draw what we can of next tablet
//    * move the focused tablet againt the direction of travel if necessary
//    * prefer the space in the direction of walking
//    * last tablet drawn is 'backstop'
//  * if out of space or tablets, we're done
//  * walk the list in the direction against travel, foc->focw
//    * if focw == backstop, we're done
//    * draw through edge

static int
draw_borders(ncplane* n, unsigned mask, uint64_t channel, direction_e direction){
  unsigned lenx, leny;
  ncplane_dim_yx(n, &leny, &lenx);
  int maxx = lenx - 1;
  int maxy = leny - 1;
  nccell ul, ur, ll, lr, hl, vl;
  nccell_init(&ul); nccell_init(&ur); nccell_init(&hl);
  nccell_init(&ll); nccell_init(&lr); nccell_init(&vl);
  if(nccells_rounded_box(n, 0, channel, &ul, &ur, &ll, &lr, &hl, &vl)){
    return -1;
  }
//fprintf(stderr, "drawing borders %p ->%d/%d, mask: %04x\n", w, maxx, maxy, mask);
  // lenx is the number of columns we have, but drop 2 due to corners. we thus
  // want lenx - 2 horizontal lines in a top or bottom border.
  int y = 0;
  if(y < maxy || direction == DIRECTION_DOWN || (mask & NCBOXMASK_BOTTOM)){
    if(!(mask & NCBOXMASK_TOP)){
      ncplane_home(n);
      ncplane_putc(n, &ul);
      ncplane_hline(n, &hl, lenx - 2);
      ncplane_putc(n, &ur);
      ++y;
    }
  }
  // draw the vertical sides, assuming they're not masked out. start wherever
  // we're left following the previous stanza, end based on maxhorizy.
  const bool candrawbottom = y <= maxy || direction == DIRECTION_UP || (mask & NCBOXMASK_TOP);
  const int maxhorizy = maxy - (candrawbottom && !(mask & NCBOXMASK_BOTTOM));
  int ret = 0;
  while(y <= maxhorizy){
    if(!(mask & NCBOXMASK_LEFT)){
      ret |= ncplane_cursor_move_yx(n, y, 0);
      ncplane_putc(n, &vl);
    }
    if(!(mask & NCBOXMASK_RIGHT)){
      ret |= ncplane_cursor_move_yx(n, y, maxx);
      ncplane_putc(n, &vl);
    }
    ++y;
  }
  if(candrawbottom){
    if(!(mask & NCBOXMASK_BOTTOM)){
      ret |= ncplane_cursor_move_yx(n, maxy, 0);
      ncplane_putc(n, &ll);
      ncplane_hline(n, &hl, lenx - 2);
      ncplane_putc(n, &lr);
    }
  }
  nccell_release(n, &ul); nccell_release(n, &ur); nccell_release(n, &hl);
  nccell_release(n, &ll); nccell_release(n, &lr); nccell_release(n, &vl);
  return ret;
}

// Draws the border (if one should be drawn) around the ncreel, and enforces
// any provided restrictions on visible window size.
static int
draw_ncreel_borders(const ncreel* nr){
  return draw_borders(nr->p, nr->ropts.bordermask, nr->ropts.borderchan,
                      DIRECTION_UP); // direction shouldn't matter for reel
}

// Calculate the starting and ending coordinates available for occupation by
// the tablet, relative to the ncreel's ncplane. Returns non-zero if the
// tablet cannot be made visible as specified. If this is the focused tablet
// (nr->tablets == t), it can take the entire reel -- frontiery is only a
// suggestion in this case -- so give it the full breadth.
static int
tablet_geom(const ncreel* nr, nctablet* t, int* begx, int* begy,
            int* lenx, int* leny, int frontiertop, int frontierbottom,
            direction_e direction){
//fprintf(stderr, "jigsawing %p with %d/%d dir %d\n", t, frontiertop, frontierbottom, direction);
  *begy = 0;
  *begx = 0;
  unsigned uleny, ulenx;
  ncplane_dim_yx(nr->p, &uleny, &ulenx);
  *leny = uleny;
  *lenx = ulenx;
  if(frontiertop < 0){
    if(direction == DIRECTION_UP){
      return -1;
    }
    frontiertop = 0;
  }
  if(frontierbottom >= *leny){
    if(direction == DIRECTION_DOWN){
      return -1;
    }
    frontierbottom = *leny - 1;
  }
  // account for the ncreel borders on the sides
  if(!(nr->ropts.bordermask & NCBOXMASK_LEFT)){
    ++*begx;
    --*lenx;
  }
  if(!(nr->ropts.bordermask & NCBOXMASK_RIGHT)){
    --*lenx;
  }
  if(!(nr->ropts.bordermask & NCBOXMASK_TOP)){
    ++*begy;
    --*leny;
  }
  if(!(nr->ropts.bordermask & NCBOXMASK_BOTTOM)){
    --*leny;
  }
  // at this point, our coordinates describe the largest possible tablet for
  // this ncreel. this is the correct solution for the focused tablet. other
  // tablets can only grow in one of two directions, so tighten them up.
  if(nr->tablets != t){
    *leny -= (frontierbottom - (frontiertop + 1));
    if(direction == DIRECTION_DOWN){
      *begy = frontierbottom;
    }else{
      *begy = frontiertop - *leny;
    }
  }
  if(*leny <= 0 || *lenx <= 0){
    return -1;
  }
  return 0;
}

// kill the planes associated with |t|
static void
nctablet_wipeout(nctablet* t){
  if(t){
    if(ncplane_set_widget(t->p, NULL, NULL) == 0){
      ncplane_destroy_family(t->p);
    }
    t->p = NULL;
    t->cbp = NULL;
  }
}

// destroy all existing tablet planes pursuant to redraw
static void
clean_reel(ncreel* r){
  nctablet* vft = r->vft;
  if(vft){
    for(nctablet* n = vft->next ; n->p && n != vft ; n = n->next){
//fprintf(stderr, "CLEANING NEXT: %p (%p)\n", n, n->p);
      nctablet_wipeout(n);
    }
    for(nctablet* n = vft->prev ; n->p && n != vft ; n = n->prev){
//fprintf(stderr, "CLEANING PREV: %p (%p)\n", n, n->p);
      nctablet_wipeout(n);
    }
//fprintf(stderr, "CLEANING VFT: %p (%p)\n", vft, vft->p);
    nctablet_wipeout(vft);
    r->vft = NULL;
  }
}

// used as ncplane widget destructor callback, and likewise the heart of
// ncreel_del(). without this, at context collapse time we'd wipe out all the
// planes (without wiping out their subsidiaries), and ncreel_destroy() would
// walk freed memory. as we have no handle on the ncreel, we do not update its
// metadata (tabletcount), nor redraw, but who cares? we're shutting down.
static void
nctablet_delete_internal(struct nctablet* t){
  t->prev->next = t->next;
  t->next->prev = t->prev;
  if(t->p){
    if(ncplane_set_widget(t->p, NULL, NULL) == 0){
      ncplane_destroy_family(t->p);
    }
  }
  free(t);
}

int ncreel_del(ncreel* nr, struct nctablet* t){
  if(t == NULL){
    return -1;
  }
  if(nr->tablets == t){
    if((nr->tablets = t->next) == t){
      nr->tablets = NULL;
    }
    // FIXME ought set direction based on actual location of replacement tablet
    nr->direction = LASTDIRECTION_DOWN;
  }
  if(nr->vft == t){
    clean_reel(nr); // maybe just make nr->tablets the vft?
  }
  nctablet_delete_internal(t);
  --nr->tabletcount;
  ncreel_redraw(nr);
  return 0;
}

// Draw the specified tablet, if possible. DIRECTION_UP means we're laying out
// bottom-to-top. DIRECTION_DOWN means top-to-bottom. 'frontier{top, bottom}'
// are the lines to which we'll be fitting the tablet ('frontiertop' to our
// last row for DIRECTION_UP, and 'frontierbottom' to our first row for
// DIRECTION_DOWN). Gives the tablet all possible space to work with (i.e.
// everything beyond the frontiers, or the entire reel for the focused tablet).
// If the callback uses less space, shrinks the plane to that size.
static int
ncreel_draw_tablet(const ncreel* nr, nctablet* t, int frontiertop,
                   int frontierbottom, direction_e direction){
  if(t->p || t->cbp){
//fprintf(stderr, "already drew %p: %p %p\n", t, t->p, t->cbp);
    return -1;
  }
  int lenx, leny, begy, begx;
  if(tablet_geom(nr, t, &begx, &begy, &lenx, &leny, frontiertop, frontierbottom, direction)){
//fprintf(stderr, "no room: %p base %d/%d len %d/%d dir %d\n", t, begy, begx, leny, lenx, direction);
    return -1;
  }
//fprintf(stderr, "p tplacement: %p base %d/%d len %d/%d frontiery %d %d dir %d\n", t, begy, begx, leny, lenx, frontiertop, frontierbottom, direction);
  struct ncplane_options nopts = {
    .y = begy,
    .x = begx,
    .rows = leny,
    .cols = lenx,
    .name = "tab",
  };
  ncplane* fp = ncplane_create(nr->p, &nopts);
  if((t->p = fp) == NULL){
//fprintf(stderr, "failure creating border plane %d %d %d %d\n", leny, lenx, begy, begx);
    return -1;
  }
  ncplane_set_widget(t->p, t, (void(*)(void*))nctablet_delete_internal);
  // we allow the callback to use a bound plane that lives above our border
  // plane, thus preventing the callback from spilling over the tablet border.
  int cby = 0, cbx = 0, cbleny = leny, cblenx = lenx;
  //cbleny -= !(nr->ropts.tabletmask & NCBOXMASK_BOTTOM);
  // FIXME shouldn't this be instead "drop 1 if either is unmasked"?
  if(!(nr->ropts.tabletmask & NCBOXMASK_TOP)){
    --cbleny;
    ++cby;
  }
  cblenx -= !(nr->ropts.tabletmask & NCBOXMASK_RIGHT);
  if(!(nr->ropts.tabletmask & NCBOXMASK_LEFT)){
    --cblenx;
    ++cbx;
  }
  if(cbleny - cby + 1 > 0){
//fprintf(stderr, "cbp placement %dx%d @ %dx%d\n", cbleny, cblenx, cby, cbx);
    struct ncplane_options dnopts = {
      .y = cby,
      .x = cbx,
      .rows = cbleny,
      .cols = cblenx,
      .name = "tdat",
    };
    t->cbp = ncplane_create(t->p, &dnopts);
    if(t->cbp == NULL){
//fprintf(stderr, "failure creating data plane %d %d %d %d\n", cbleny, cblenx, cby, cbx);
      ncplane_destroy(t->p);
      t->p = NULL;
      return -1;
    }
    ncplane_move_above(t->cbp, t->p);
//fprintf(stderr, "calling! lenx/leny: %d/%d cbx/cby: %d/%d cblenx/cbleny: %d/%d dir: %d\n", lenx, leny, cbx, cby, cblenx, cbleny, direction);
    int ll = t->cbfxn(t, direction == DIRECTION_DOWN);
//fprintf(stderr, "RETURNRETURNRETURN %p %d (%d, %d, %d) DIR %d\n", t, ll, cby, cbleny, leny, direction);
    if(ll > cbleny){
      logwarn("tablet callback returned %d lines, %d allowed", ll, cbleny);
      ll = cbleny;
    }
    if(ll != cbleny){
      int diff = cbleny - ll;
//fprintf(stderr, "resizing data plane %d->%d\n", cbleny, ll);
      if(ll){ // must be smaller than the space we provided; add back bottom
        ncplane_resize_simple(t->cbp, ll, cblenx);
      }else{
        ncplane_destroy_family(t->cbp);
        t->cbp = NULL;
      }
      // resize the borderplane iff we got smaller
      if(!(nr->ropts.tabletmask & NCBOXMASK_BOTTOM)){
        ncplane_resize_simple(t->p, leny - diff + 1, lenx);
      }else{
        ncplane_resize_simple(t->p, leny - diff, lenx);
      }
      // We needn't move the resized plane if drawing down, or the focused plane.
      // The focused tablet will have been resized properly above, but it might
      // be out of position (the focused tablet ought move as little as possible).
      // Move it back to the frontier, or the nearest line above if it has grown.
      if(nr->tablets == t){
        if(leny - frontiertop + 1 < ll){
          ncplane_yx(fp, &frontiertop, NULL);
          frontiertop += (leny - ll);
        }
        ncplane_move_yx(fp, frontiertop, begx);
//fprintf(stderr, "moved to frontiertop %d\n", frontiertop);
      }else if(direction == DIRECTION_UP){
        ncplane_move_yx(fp, begy + diff, begx);
//fprintf(stderr, "MOVEDOWN from %d to %d\n", begy, begy + diff);
      }
      cbleny = ll;
    }
  }
  // we can't push the border plane beyond its true boundaries, or we'll mess
  // up layout later. instead, add a bottommask iff leny <= cbleny + 1
  unsigned mask = nr->ropts.tabletmask;
  if(leny <= cbleny + !(mask & NCBOXMASK_TOP)){
    mask |= NCBOXMASK_BOTTOM;
  }
  uint64_t channels = nr->tablets == t ? nr->ropts.focusedchan : nr->ropts.tabletchan;
  draw_borders(fp, mask, channels, direction);
  return 0;
}

// move down below the focused tablet, filling up the reel to the bottom.
// returns the last tablet drawn.
static nctablet*
draw_following_tablets(const ncreel* nr, nctablet* otherend,
                       int frontiertop, int* frontierbottom){
  const bool botborder = !(nr->ropts.bordermask & NCBOXMASK_BOTTOM);
//fprintf(stderr, "following otherend: %p ->p: %p %d/%d\n", otherend, otherend->p, frontiertop, *frontierbottom);
  nctablet* working = nr->tablets->next;
  const int maxx = ncplane_dim_y(nr->p) - 1 - botborder;
  // move down past the focused tablet, filling up the reel to the bottom
  while(*frontierbottom <= maxx && (working != otherend || !otherend->p)){
    if(working->p){
      break;
    }
//fprintf(stderr, "following otherend: %p ->p: %p\n", otherend, otherend->p);
    // modify frontier based off the one we're at
//fprintf(stderr, "EASTBOUND AND DOWN: %p->%p %d %d\n", working, working->next, frontiertop, *frontierbottom);
    if(ncreel_draw_tablet(nr, working, frontiertop, *frontierbottom, DIRECTION_DOWN)){
      return NULL;
    }
    if(working == otherend){
      otherend = otherend->next;
    }
    *frontierbottom += ncplane_dim_y(working->p) + 1;
    working = working->next;
  }
//fprintf(stderr, "done with prevs: %p->%p %d %d\n", working, working->prev, frontiertop, *frontierbottom);
  return working;
}

// move up above the focused tablet, filling up the reel to the top.
// returns the last tablet drawn.
static nctablet*
draw_previous_tablets(const ncreel* nr, nctablet* otherend,
                      int* frontiertop, int frontierbottom){
  const bool topborder = !(nr->ropts.bordermask & NCBOXMASK_TOP);
  nctablet* upworking = nr->tablets->prev;
//fprintf(stderr, "preceding %p otherend: %p ->p: %p frontiers: %d %d\n", upworking, otherend, otherend->p, *frontiertop, frontierbottom);
  // modify frontier based off the one we're at
  while(*frontiertop >= topborder && (upworking != otherend || !otherend->p)){
    if(upworking->p){
      break;
    }
//fprintf(stderr, "MOVIN' ON UP: %p->%p %d %d\n", upworking, upworking->prev, *frontiertop, frontierbottom);
    if(ncreel_draw_tablet(nr, upworking, *frontiertop, frontierbottom, DIRECTION_UP)){
      return NULL;
    }
    if(upworking == otherend){
      otherend = otherend->prev;
    }
    *frontiertop -= ncplane_dim_y(upworking->p) + 1;
    upworking = upworking->prev;
  }
//fprintf(stderr, "done with prevs: %p->%p %d %d\n", upworking, upworking->prev, *frontiertop, frontierbottom);
  return upworking;
}

// Tablets are initially drawn assuming more space to be available than may
// actually exist. We do a pass at the end trimming any overhang.
static int
trim_reel_overhang(ncreel* r, nctablet* top, nctablet* bottom){
  int y;
  if(!top || !top->p || !bottom || !bottom->p){
    return -1;
  }
//fprintf(stderr, "trimming: top %p bottom %p\n", top->p, bottom->p);
  ncplane_yx(top->p, &y, NULL);
  unsigned ylen, xlen;
  ncplane_dim_yx(top->p, &ylen, &xlen);
  const int miny = !(r->ropts.bordermask & NCBOXMASK_TOP);
  int boty = y + ylen - 1;
//fprintf(stderr, "top: %dx%d @ %d, miny: %d\n", ylen, xlen, y, miny);
  if(boty < miny){
//fprintf(stderr, "NUKING top!\n");
    ncplane_destroy_family(top->p);
    top->p = NULL;
    top->cbp = NULL;
    top = top->next;
    return trim_reel_overhang(r, top, bottom);
  }else if(y < miny){
    int ynew = ylen - (miny - y);
    if(ynew <= 0){
      ncplane_destroy_family(top->p);
      top->p = NULL;
      top->cbp = NULL;
    }else{
      if(ncplane_resize(top->p, miny - y, 0, ynew, xlen, 0, 0, ynew, xlen)){
        return -1;
      }
      if(top->cbp){
        if(ynew == !(r->ropts.tabletmask & NCBOXMASK_TOP)){
          ncplane_destroy_family(top->cbp);
          top->cbp = NULL;
        }else{
          ncplane_dim_yx(top->cbp, &ylen, &xlen);
          ynew -= !(r->ropts.tabletmask & NCBOXMASK_TOP);
          if(ncplane_resize(top->cbp, miny - y, 0, ynew, xlen, 0, 0, ynew, xlen)){
            return -1;
          }
          int x;
          ncplane_yx(top->cbp, &y, &x);
          ncplane_move_yx(top->cbp, y - 1, x);
        }
      }
    }
  }
  if(bottom->p){
    ncplane_dim_yx(bottom->p, &ylen, &xlen);
    ncplane_yx(bottom->p, &y, NULL);
    const int maxy = ncplane_dim_y(r->p) - (1 + !(r->ropts.bordermask & NCBOXMASK_BOTTOM));
    boty = y + ylen - 1;
  //fprintf(stderr, "bot: %dx%d @ %d, maxy: %d\n", ylen, xlen, y, maxy);
    if(maxy < y){
  //fprintf(stderr, "NUKING bottom!\n");
      if(ncplane_set_widget(bottom->p, NULL, NULL) == 0){
        ncplane_destroy_family(bottom->p);
      }
      bottom->p = NULL;
      bottom->cbp = NULL;
      bottom = bottom->prev;
      return trim_reel_overhang(r, top, bottom);
    }if(maxy < boty){
      int ynew = ylen - (boty - maxy);
      if(ynew <= 0){
        ncplane_destroy_family(bottom->p);
        bottom->p = NULL;
        bottom->cbp = NULL;
      }else{
        if(ncplane_resize(bottom->p, 0, 0, ynew, xlen, 0, 0, ynew, xlen)){
          return -1;
        }
  //fprintf(stderr, "TRIMMED bottom %p from %d to %d (%d)\n", bottom->p, ylen, ynew, maxy - boty);
        if(bottom->cbp){
          if(ynew == !(r->ropts.tabletmask & NCBOXMASK_BOTTOM)){
            ncplane_destroy_family(bottom->cbp);
            bottom->cbp = NULL;
          }else{
            ncplane_dim_yx(bottom->cbp, &ylen, &xlen);
            ynew -= !(r->ropts.tabletmask & NCBOXMASK_BOTTOM);
            if(ncplane_resize(bottom->cbp, 0, 0, ynew, xlen, 0, 0, ynew, xlen)){
              return -1;
            }
          }
        }
      }
    }
  }
//fprintf(stderr, "finished trimming\n");
  return 0;
}

static int
tighten_reel_down(ncreel* r, int ybot){
  nctablet* cur = r->tablets;
  while(cur){
    if(cur->p == NULL){
      break;
    }
    int cury, curx;
    unsigned ylen;
    ncplane_yx(cur->p, &cury, &curx);
    ncplane_dim_yx(cur->p, &ylen, NULL);
    if(cury <= ybot - (int)ylen - 1){
      break;
    }
//fprintf(stderr, "tightening %p down to %d from %d\n", cur, ybot - ylen, cury);
    cury = ybot - (int)ylen;
    ncplane_move_yx(cur->p, cury, curx);
    ybot = cury - 1;
    if((cur = cur->prev) == r->tablets){
      break;
    }
  }
  return 0;
}

// run at the end of redraw, this aligns the top tablet with the top of the
// reel. we prefer empty space at the bottom (FIXME but not really -- we ought
// prefer space away from the last direction of movement. rather than this
// postprocessing, draw things to the right places!). we then trim any tablet
// overhang. FIXME could pass top/bottom in directly, available as otherend
static int
tighten_reel(ncreel* r){
//fprintf(stderr, "tightening it up\n");
  nctablet* top = r->tablets;
  nctablet* cur = top;
  int ytop = INT_MAX;
  // find the top tablet
  while(cur){
    if(cur->p == NULL){
      break;
    }
    int cury;
    ncplane_yx(cur->p, &cury, NULL);
    if(cury >= ytop){
      break;
    }
    ytop = cury;
    top = cur;
    cur = cur->prev;
  }
  int expected = !(r->ropts.bordermask & NCBOXMASK_TOP);
  cur = top;
  nctablet* bottom = r->tablets;
  // find the bottom tablet, moving tablets up as we go along
  while(cur){
    if(cur->p == NULL){
      break;
    }
    int cury, curx;
    ncplane_yx(cur->p, &cury, &curx);
    if(cury != expected){
      if(ncplane_move_yx(cur->p, expected, curx)){
//fprintf(stderr, "tightened %p up to %d\n", cur, expected);
        return -1;
      }
    }
    unsigned ylen;
    ncplane_dim_yx(cur->p, &ylen, NULL);
    expected += ylen + 1;
//fprintf(stderr, "bottom (%p) gets %p\n", bottom, cur);
    bottom = cur;
    cur = cur->next;
    if(cur == top){
      break;
    }
  }
  cur = r->tablets;
  if(cur){
    const ncplane* n = cur->p;
    int yoff;
    unsigned ylen, rylen;
    ncplane_dim_yx(r->p, &rylen, NULL);
    ncplane_yx(n, &yoff, NULL);
    ncplane_dim_yx(n, &ylen, NULL);
    const int ybot = rylen - 1 + !!(r->ropts.bordermask & NCBOXMASK_BOTTOM);
    // FIXME want to tighten down whenever we're at the bottom, and the reel
    // is full, not just in this case (this can leave a gap of more than 1 row)
    if(yoff + (int)ylen + 1 >= ybot){
      if(tighten_reel_down(r, ybot)){
        return -1;
      }
    }
  }
  if(!top || !bottom){
    return 0;
  }
  return trim_reel_overhang(r, top, bottom);
}

// Arrange the panels, starting with the focused window, wherever it may be.
// If necessary, resize it to the full size of the reel--focus has its
// privileges. We then work in the opposite direction of travel, filling out
// the reel above and below. If we moved down to get here, do the tablets above
// first. If we moved up, do the tablets below. This ensures tablets stay in
// place relative to the new focus; they could otherwise pivot around the new
// focus, if we're not filling out the reel.
int ncreel_redraw(ncreel* nr){
//fprintf(stderr, "\n--------> BEGIN REDRAW <--------\n");
  nctablet* focused = nr->tablets;
  int fulcrum; // target line
  if(nr->direction == LASTDIRECTION_UP){
    // case i iff the last direction was UP, and either the focused tablet is
    // not visible, or below the visibly-focused tablet, or there is no
    // visibly-focused tablet.
    if(!focused || !focused->p || !nr->vft){
      fulcrum = 0;
//fprintf(stderr, "case i fulcrum %d\n", fulcrum);
    }else{
      int focy, vfty;
      ncplane_yx(focused->p, &focy, NULL);
      ncplane_yx(nr->vft->p, &vfty, NULL);
      if(focy > vfty){
        fulcrum = 0;
//fprintf(stderr, "case i fulcrum %d (%d %d) %p %p\n", fulcrum, focy, vfty, focused, nr->vft);
      }else{
        ncplane_yx(focused->p, &fulcrum, NULL); // case iii
//fprintf(stderr, "case iii fulcrum %d (%d %d) %p %p lastdir: %d\n", fulcrum, focy, vfty, focused, nr->vft, nr->direction);
      }
    }
  }else{
    // case ii iff the last direction was DOWN, and either the focused tablet
    // is not visible, or above the visibly-focused tablet, or there is no
    // visibly-focused tablet.
    if(!focused || !focused->p || !nr->vft){
      fulcrum = ncplane_dim_y(nr->p) - 1;
//fprintf(stderr, "case ii fulcrum %d\n", fulcrum);
    }else{
      int focy, vfty;
//fprintf(stderr, "focused: %p\n", focused);
      ncplane_yx(focused->p, &focy, NULL);
      ncplane_yx(nr->vft->p, &vfty, NULL);
      if(focy < vfty){
        fulcrum = ncplane_dim_y(nr->p) - 1;
//fprintf(stderr, "case ii fulcrum %d (%d %d) %p %p\n", fulcrum, focy, vfty, focused, nr->vft);
      }else{
        ncplane_yx(focused->p, &fulcrum, NULL); // case iii
//fprintf(stderr, "case iiib fulcrum %d (%d %d) %p %p lastdir: %d\n", fulcrum, focy, vfty, focused, nr->vft, nr->direction);
      }
    }
  }
  clean_reel(nr);
  if(focused){
//fprintf(stderr, "drawing focused tablet %p dir: %d fulcrum: %d!\n", focused, nr->direction, fulcrum);
    if(ncreel_draw_tablet(nr, focused, fulcrum, fulcrum, DIRECTION_DOWN)){
      logerror("error drawing tablet");
      return -1;
    }
//fprintf(stderr, "drew focused tablet %p -> %p lastdir: %d!\n", focused, focused->p, nr->direction);
    nctablet* otherend = focused;
    int frontiertop, frontierbottom;
    ncplane_yx(nr->tablets->p, &frontiertop, NULL);
    frontierbottom = frontiertop + ncplane_dim_y(nr->tablets->p) + 1;
    frontiertop -= 2;
    if(nr->direction == LASTDIRECTION_DOWN){
      otherend = draw_previous_tablets(nr, otherend, &frontiertop, frontierbottom);
      if(otherend == NULL){
        logerror("error drawing higher tablets");
        return -1;
      }
      otherend = draw_following_tablets(nr, otherend, frontiertop, &frontierbottom);
    }else{ // DIRECTION_UP
      otherend = draw_previous_tablets(nr, otherend, &frontiertop, frontierbottom);
      if(otherend == NULL){
        logerror("error drawing higher tablets");
        return -1;
      }
      otherend = draw_following_tablets(nr, otherend, frontiertop, &frontierbottom);
    }
    if(otherend == NULL){
      logerror("error drawing following tablets");
      return -1;
    }
//notcurses_debug(ncplane_notcurses(nr->p), stderr);
    if(tighten_reel(nr)){
      logerror("error tightening reel");
      return -1;
    }
//notcurses_debug(ncplane_notcurses(nr->p), stderr);
  }
  nr->vft = nr->tablets; // update the visually-focused tablet pointer
//fprintf(stderr, "DONE ARRANGING\n");
  if(draw_ncreel_borders(nr)){
    logerror("error drawing reel borders");
    return -1; // enforces specified dimensional minima
  }
  return 0;
}

static bool
validate_ncreel_opts(ncplane* n, const ncreel_options* ropts){
  if(n == NULL){
    return false;
  }
  if(ropts->flags >= (NCREEL_OPTION_CIRCULAR << 1u)){
    logwarn("provided unsupported flags 0x%016" PRIx64, ropts->flags);
  }
  if(ropts->flags & NCREEL_OPTION_CIRCULAR){
    if(!(ropts->flags & NCREEL_OPTION_INFINITESCROLL)){
      logerror("can't set circular without infinitescroll");
      return false; // can't set circular without infinitescroll
    }
  }
  // there exist higher NCBOX defines, but they're not valid in this context
  const unsigned fullmask = NCBOXMASK_LEFT |
                            NCBOXMASK_RIGHT |
                            NCBOXMASK_TOP |
                            NCBOXMASK_BOTTOM;
  if(ropts->bordermask > fullmask){
    logerror("bad bordermask: 0x%016x", ropts->bordermask);
    return false;
  }
  if(ropts->tabletmask > fullmask){
    logerror("bad tabletmask: 0x%016x", ropts->bordermask);
    return false;
  }
  return true;
}

ncplane* nctablet_plane(nctablet* t){
  return t->cbp;
}

ncplane* ncreel_plane(ncreel* nr){
  return nr->p;
}

ncreel* ncreel_create(ncplane* n, const ncreel_options* ropts){
  ncreel_options zeroed = {0};
  ncreel* nr;
  if(!ropts){
    ropts = &zeroed;
  }
  if(!validate_ncreel_opts(n, ropts)){
    return NULL;
  }
  if((nr = malloc(sizeof(*nr))) == NULL){
    return NULL;
  }
  nr->tablets = NULL;
  nr->tabletcount = 0;
  nr->direction = LASTDIRECTION_DOWN; // draw down after the initial tablet
  memcpy(&nr->ropts, ropts, sizeof(*ropts));
  nr->p = n;
  nr->vft = NULL;
  if(ncplane_set_widget(nr->p, nr, (void(*)(void*))ncreel_destroy)){
    ncplane_destroy(nr->p);
    free(nr);
    return NULL;
  }
  if(ncreel_redraw(nr)){
    ncplane_destroy(nr->p);
    free(nr);
    return NULL;
  }
  return nr;
}

nctablet* ncreel_add(ncreel* nr, nctablet* after, nctablet *before,
                     tabletcb cbfxn, void* opaque){
  nctablet* t;
  if(after && before){
    if(after->next != before || before->prev != after){
      logerror("bad before (%p) / after (%p) spec", before, after);
      return NULL;
    }
  }else if(!after && !before){
    // This way, without user interaction or any specification, new tablets are
    // inserted at the "end" relative to the focus. The first one to be added
    // gets and keeps the focus. New ones will go on the bottom, until we run
    // out of space. New tablets are then created off-screen.
    before = nr->tablets;
  }
  if((t = malloc(sizeof(*t))) == NULL){
    return NULL;
  }
//fprintf(stderr, "--------->NEW TABLET %p\n", t);
  if(after){
    t->next = after->next;
    after->next = t;
    t->prev = after;
    t->next->prev = t;
  }else if(before){
    t->prev = before->prev;
    before->prev = t;
    t->next = before;
    t->prev->next = t;
  }else{ // we're the first tablet
    t->prev = t->next = t;
    nr->tablets = t;
  }
  t->cbfxn = cbfxn;
  t->curry = opaque;
  ++nr->tabletcount;
  t->p = NULL;
  t->cbp = NULL;
  ncreel_redraw(nr);
  return t;
}

void ncreel_destroy(ncreel* nreel){
  if(nreel){
    if(ncplane_set_widget(nreel->p, NULL, NULL) == 0){
      nctablet* t;
      while( (t = nreel->tablets) ){
        ncreel_del(nreel, t);
      }
      ncplane_destroy(nreel->p);
    }
    free(nreel);
  }
}

void* nctablet_userptr(nctablet* t){
  return t->curry;
}

int ncreel_tabletcount(const ncreel* nreel){
  return nreel->tabletcount;
}

nctablet* ncreel_focused(ncreel* nr){
  return nr->tablets;
}

nctablet* ncreel_next(ncreel* nr){
  if(nr->tablets){
    nr->tablets = nr->tablets->next;
//fprintf(stderr, "---------------> moved to next, %p to %p <----------\n", nr->tablets->prev, nr->tablets);
    nr->direction = LASTDIRECTION_DOWN;
    ncreel_redraw(nr);
  }
  return nr->tablets;
}

nctablet* ncreel_prev(ncreel* nr){
  if(nr->tablets){
    nr->tablets = nr->tablets->prev;
//fprintf(stderr, "----------------> moved to prev, %p to %p <----------\n", nr->tablets->next, nr->tablets);
    nr->direction = LASTDIRECTION_UP;
    ncreel_redraw(nr);
  }
  return nr->tablets;
}

bool ncreel_offer_input(ncreel* n, const ncinput* nc){
  if(nc->evtype == NCTYPE_RELEASE){
    return false;
  }
  if(nc->id == NCKEY_UP){
    ncreel_prev(n);
    return true;
  }else if(nc->id == NCKEY_DOWN){
    ncreel_next(n);
    return true;
  }else if(nc->id == NCKEY_SCROLL_UP){
    ncreel_prev(n);
    return true;
  }else if(nc->id == NCKEY_SCROLL_DOWN){
    ncreel_next(n);
    return true;
  }
  // FIXME page up/page down
  // FIXME end/home (when not using infinite scrolling)
  return false;
}
