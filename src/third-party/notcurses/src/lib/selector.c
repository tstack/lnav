#include "internal.h"

// internal ncselector item
struct ncselector_int {
  char* option;
  char* desc;
  size_t opcolumns;   // filled in by library
  size_t desccolumns; // filled in by library
};

struct ncmselector_int {
  char* option;
  char* desc;
  bool selected;
};

typedef struct ncselector {
  ncplane* ncp;                  // backing ncplane
  unsigned selected;             // index of selection
  unsigned startdisp;            // index of first option displayed
  unsigned maxdisplay;           // max number of items to display, 0 -> no limit
  unsigned longop;               // columns occupied by longest option
  unsigned longdesc;             // columns occupied by longest description
  struct ncselector_int* items;  // list of items and descriptions, heap-copied
  unsigned itemcount;            // number of pairs in 'items'
  char* title;                   // can be NULL, in which case there's no riser
  unsigned titlecols;            // columns occupied by title
  char* secondary;               // can be NULL
  unsigned secondarycols;        // columns occupied by secondary
  char* footer;                  // can be NULL
  unsigned footercols;           // columns occupied by footer
  uint64_t opchannels;           // option channels
  uint64_t descchannels;         // description channels
  uint64_t titlechannels;        // title channels
  uint64_t footchannels;         // secondary and footer channels
  uint64_t boxchannels;          // border channels
  int uarrowy, darrowy, arrowx;// location of scrollarrows, even if not present
} ncselector;

typedef struct ncmultiselector {
  ncplane* ncp;                   // backing ncplane
  unsigned current;               // index of highlighted item
  unsigned startdisp;             // index of first option displayed
  unsigned maxdisplay;            // max number of items to display, 0 -> no limit
  unsigned longitem;              // columns occupied by longest item
  struct ncmselector_int* items;  // items, descriptions, and statuses, heap-copied
  unsigned itemcount;             // number of pairs in 'items'
  char* title;                    // can be NULL, in which case there's no riser
  unsigned titlecols;             // columns occupied by title
  char* secondary;                // can be NULL
  unsigned secondarycols;         // columns occupied by secondary
  char* footer;                   // can be NULL
  unsigned footercols;            // columns occupied by footer
  uint64_t opchannels;            // option channels
  uint64_t descchannels;          // description channels
  uint64_t titlechannels;         // title channels
  uint64_t footchannels;          // secondary and footer channels
  uint64_t boxchannels;           // border channels
  int uarrowy, darrowy, arrowx;   // location of scrollarrows, even if not present
} ncmultiselector;

// ideal body width given the ncselector's items and secondary/footer
static int
ncselector_body_width(const ncselector* n){
  unsigned cols = 0;
  // the body is the maximum of
  //  * longop + longdesc + 5
  //  * secondary + 2
  //  * footer + 2
  if(n->footercols + 2 > cols){
    cols = n->footercols + 2;
  }
  if(n->secondarycols + 2 > cols){
    cols = n->secondarycols + 2;
  }
  if(n->longop + n->longdesc + 5 > cols){
    cols = n->longop + n->longdesc + 5;
  }
  return cols;
}

// redraw the selector widget in its entirety
static int
ncselector_draw(ncselector* n){
  ncplane_erase(n->ncp);
  nccell transchar = NCCELL_TRIVIAL_INITIALIZER;
  nccell_set_fg_alpha(&transchar, NCALPHA_TRANSPARENT);
  nccell_set_bg_alpha(&transchar, NCALPHA_TRANSPARENT);
  // if we have a title, we'll draw a riser. the riser is two rows tall, and
  // exactly four columns longer than the title, and aligned to the right. we
  // draw a rounded box. the body will blow part or all of the bottom away.
  int yoff = 0;
  if(n->title){
    size_t riserwidth = n->titlecols + 4;
    int offx = ncplane_halign(n->ncp, NCALIGN_RIGHT, riserwidth);
    ncplane_cursor_move_yx(n->ncp, 0, 0);
    if(offx){
      ncplane_hline(n->ncp, &transchar, offx);
    }
    ncplane_cursor_move_yx(n->ncp, 0, offx);
    ncplane_rounded_box_sized(n->ncp, 0, n->boxchannels, 3, riserwidth, 0);
    n->ncp->channels = n->titlechannels;
    ncplane_printf_yx(n->ncp, 1, offx + 1, " %s ", n->title);
    yoff += 2;
    ncplane_cursor_move_yx(n->ncp, 1, 0);
    if(offx){
      ncplane_hline(n->ncp, &transchar, offx);
    }
  }
  unsigned bodywidth = ncselector_body_width(n);
  unsigned dimy, dimx;
  ncplane_dim_yx(n->ncp, &dimy, &dimx);
  int xoff = ncplane_halign(n->ncp, NCALIGN_RIGHT, bodywidth);
  if(xoff){
    for(unsigned y = yoff + 1 ; y < dimy ; ++y){
      ncplane_cursor_move_yx(n->ncp, y, 0);
      ncplane_hline(n->ncp, &transchar, xoff);
    }
  }
  ncplane_cursor_move_yx(n->ncp, yoff, xoff);
  ncplane_rounded_box_sized(n->ncp, 0, n->boxchannels, dimy - yoff, bodywidth, 0);
  if(n->title){
    n->ncp->channels = n->boxchannels;
    if(notcurses_canutf8(ncplane_notcurses(n->ncp))){
      ncplane_putegc_yx(n->ncp, 2, dimx - 1, "┤", NULL);
    }else{
      ncplane_putchar_yx(n->ncp, 2, dimx - 1, '|');
    }
    if(bodywidth < dimx){
      if(notcurses_canutf8(ncplane_notcurses(n->ncp))){
        ncplane_putegc_yx(n->ncp, 2, dimx - bodywidth, "┬", NULL);
      }else{
        ncplane_putchar_yx(n->ncp, 2, dimx - bodywidth, '-');
      }
    }
    if((n->titlecols + 4 != dimx) && n->titlecols > n->secondarycols){
      if(notcurses_canutf8(ncplane_notcurses(n->ncp))){
        ncplane_putegc_yx(n->ncp, 2, dimx - (n->titlecols + 4), "┴", NULL);
      }else{
        ncplane_putchar_yx(n->ncp, 2, dimx - (n->titlecols + 4), '-');
      }
    }
  }
  // There is always at least one space available on the right for the
  // secondary title and footer, but we'd prefer to use a few more if we can.
  if(n->secondary){
    int xloc = bodywidth - (n->secondarycols + 1) + xoff;
    if(n->secondarycols < bodywidth - 2){
      --xloc;
    }
    n->ncp->channels = n->footchannels;
    ncplane_putstr_yx(n->ncp, yoff, xloc, n->secondary);
  }
  if(n->footer){
    int xloc = bodywidth - (n->footercols + 1) + xoff;
    if(n->footercols < bodywidth - 2){
      --xloc;
    }
    n->ncp->channels = n->footchannels;
    ncplane_putstr_yx(n->ncp, dimy - 1, xloc, n->footer);
  }
  // Top line of body (background and possibly up arrow)
  ++yoff;
  ncplane_cursor_move_yx(n->ncp, yoff, xoff + 1);
  for(unsigned i = xoff + 1 ; i < dimx - 1 ; ++i){
    nccell transc = NCCELL_TRIVIAL_INITIALIZER; // fall back to base cell
    ncplane_putc(n->ncp, &transc);
  }
  const int bodyoffset = dimx - bodywidth + 2;
  if(n->maxdisplay && n->maxdisplay < n->itemcount){
    n->ncp->channels = n->descchannels;
    n->arrowx = bodyoffset + n->longop;
    if(notcurses_canutf8(ncplane_notcurses(n->ncp))){
      ncplane_putegc_yx(n->ncp, yoff, n->arrowx, "↑", NULL);
    }else{
      ncplane_putchar_yx(n->ncp, yoff, n->arrowx, '<');
    }
  }else{
    n->arrowx = -1;
  }
  n->uarrowy = yoff;
  unsigned printidx = n->startdisp;
  unsigned printed = 0;
  for(yoff += 1 ; yoff < (int)dimy - 2 ; ++yoff){
    if(n->maxdisplay && printed == n->maxdisplay){
      break;
    }
    ncplane_cursor_move_yx(n->ncp, yoff, xoff + 1);
    for(int i = xoff + 1 ; i < (int)dimx - 1 ; ++i){
      nccell transc = NCCELL_TRIVIAL_INITIALIZER; // fall back to base cell
      ncplane_putc(n->ncp, &transc);
    }
    n->ncp->channels = n->opchannels;
    if(printidx == n->selected){
      n->ncp->channels = (uint64_t)ncchannels_bchannel(n->opchannels) << 32u | ncchannels_fchannel(n->opchannels);
    }
    ncplane_printf_yx(n->ncp, yoff, bodyoffset + (n->longop - n->items[printidx].opcolumns), "%s", n->items[printidx].option);
    n->ncp->channels = n->descchannels;
    if(printidx == n->selected){
      n->ncp->channels = (uint64_t)ncchannels_bchannel(n->descchannels) << 32u | ncchannels_fchannel(n->descchannels);
    }
    ncplane_printf_yx(n->ncp, yoff, bodyoffset + n->longop, " %s", n->items[printidx].desc);
    if(++printidx == n->itemcount){
      printidx = 0;
    }
    ++printed;
  }
  // Bottom line of body (background and possibly down arrow)
  ncplane_cursor_move_yx(n->ncp, yoff, xoff + 1);
  for(int i = xoff + 1 ; i < (int)dimx - 1 ; ++i){
    nccell transc = NCCELL_TRIVIAL_INITIALIZER; // fall back to base cell
    ncplane_putc(n->ncp, &transc);
  }
  if(n->maxdisplay && n->maxdisplay < n->itemcount){
    n->ncp->channels = n->descchannels;
    if(notcurses_canutf8(ncplane_notcurses(n->ncp))){
      ncplane_putegc_yx(n->ncp, yoff, n->arrowx, "↓", NULL);
    }else{
      ncplane_putchar_yx(n->ncp, yoff, n->arrowx, '>');
    }
  }
  n->darrowy = yoff;
  return 0;
}

// calculate the necessary dimensions based off properties of the selector
static void
ncselector_dim_yx(const ncselector* n, unsigned* ncdimy, unsigned* ncdimx){
  unsigned rows = 0, cols = 0; // desired dimensions
  const ncplane* parent = ncplane_parent(n->ncp);
  unsigned dimy, dimx; // dimensions of containing plane
  ncplane_dim_yx(parent, &dimy, &dimx);
  if(n->title){ // header adds two rows for riser
    rows += 2;
  }
  // we have a top line, a bottom line, two lines of margin, and must be able
  // to display at least one row beyond that, so require five more
  rows += 5;
  rows += (!n->maxdisplay || n->maxdisplay > n->itemcount ? n->itemcount : n->maxdisplay) - 1; // rows necessary to display all options
  if(rows > dimy){ // claw excess back
    rows = dimy;
  }
  *ncdimy = rows;
  cols = ncselector_body_width(n);
  // the riser, if it exists, is header + 4. the cols are the max of these two.
  if(n->titlecols + 4 > cols){
    cols = n->titlecols + 4;
  }
  *ncdimx = cols;
}

static void
ncselector_destroy_internal(ncselector* n){
  if(n){
    while(n->itemcount--){
      free(n->items[n->itemcount].option);
      free(n->items[n->itemcount].desc);
    }
    if(ncplane_set_widget(n->ncp, NULL, NULL) == 0){
      ncplane_destroy(n->ncp);
    }
    free(n->items);
    free(n->title);
    free(n->secondary);
    free(n->footer);
    free(n);
  }
}

void ncselector_destroy(ncselector* n, char** item){
  if(n){
    if(item){
      *item = n->items[n->selected].option;
      n->items[n->selected].option = NULL;
    }
    ncselector_destroy_internal(n);
  }
}

ncselector* ncselector_create(ncplane* n, const ncselector_options* opts){
  if(n == notcurses_stdplane(ncplane_notcurses(n))){
    logerror("won't use the standard plane"); // would fail later on resize
    return NULL;
  }
  ncselector_options zeroed = {0};
  if(!opts){
    opts = &zeroed;
  }
  unsigned itemcount = 0;
  if(opts->flags > 0){
    logwarn("provided unsupported flags %016" PRIx64, opts->flags);
  }
  if(opts->items){
    for(const struct ncselector_item* i = opts->items ; i->option ; ++i){
      ++itemcount;
    }
  }
  ncselector* ns = malloc(sizeof(*ns));
  if(ns == NULL){
    return NULL;
  }
  memset(ns, 0, sizeof(*ns));
  if(opts->defidx && opts->defidx >= itemcount){
    logerror("default index %u too large (%u items)", opts->defidx, itemcount);
    goto freeitems;
  }
  ns->title = opts->title ? strdup(opts->title) : NULL;
  ns->titlecols = opts->title ? ncstrwidth(opts->title, NULL, NULL) : 0;
  ns->secondary = opts->secondary ? strdup(opts->secondary) : NULL;
  ns->secondarycols = opts->secondary ? ncstrwidth(opts->secondary, NULL, NULL) : 0;
  ns->footer = opts->footer ? strdup(opts->footer) : NULL;
  ns->footercols = opts->footer ? ncstrwidth(opts->footer, NULL, NULL) : 0;
  ns->selected = opts->defidx;
  ns->longop = 0;
  if( (ns->maxdisplay = opts->maxdisplay) ){
    if(opts->defidx >= ns->maxdisplay){
      ns->startdisp = opts->defidx - ns->maxdisplay + 1;
    }else{
      ns->startdisp = 0;
    }
  }else{
    ns->startdisp = 0;
  }
  ns->longdesc = 0;
  ns->opchannels = opts->opchannels;
  ns->boxchannels = opts->boxchannels;
  ns->descchannels = opts->descchannels;
  ns->titlechannels = opts->titlechannels;
  ns->footchannels = opts->footchannels;
  ns->boxchannels = opts->boxchannels;
  ns->darrowy = ns->uarrowy = ns->arrowx = -1;
  if(itemcount){
    if(!(ns->items = malloc(sizeof(*ns->items) * itemcount))){
      goto freeitems;
    }
  }else{
    ns->items = NULL;
  }
  for(ns->itemcount = 0 ; ns->itemcount < itemcount ; ++ns->itemcount){
    const struct ncselector_item* src = &opts->items[ns->itemcount];
    int unsafe = ncstrwidth(src->option, NULL, NULL);
    if(unsafe < 0){
      goto freeitems;
    }
    unsigned cols = unsafe;
    ns->items[ns->itemcount].opcolumns = cols;
    if(cols > ns->longop){
      ns->longop = cols;
    }
    const char *desc = src->desc ? src->desc : "";
    unsafe = ncstrwidth(desc, NULL, NULL);
    if(unsafe < 0){
      goto freeitems;
    }
    cols = unsafe;
    ns->items[ns->itemcount].desccolumns = cols;
    if(cols > ns->longdesc){
      ns->longdesc = cols;
    }
    ns->items[ns->itemcount].option = strdup(src->option);
    ns->items[ns->itemcount].desc = strdup(desc);
    if(!(ns->items[ns->itemcount].desc && ns->items[ns->itemcount].option)){
      free(ns->items[ns->itemcount].option);
      free(ns->items[ns->itemcount].desc);
      goto freeitems;
    }
  }
  unsigned dimy, dimx;
  ns->ncp = n;
  ncselector_dim_yx(ns, &dimy, &dimx);
  if(ncplane_resize_simple(n, dimy, dimx)){
    goto freeitems;
  }
  if(ncplane_set_widget(ns->ncp, ns, (void(*)(void*))ncselector_destroy_internal)){
    goto freeitems;
  }
  ncselector_draw(ns); // deal with error here?
  return ns;

freeitems:
  while(ns->itemcount--){
    free(ns->items[ns->itemcount].option);
    free(ns->items[ns->itemcount].desc);
  }
  free(ns->items);
  free(ns->title); free(ns->secondary); free(ns->footer);
  free(ns);
  ncplane_destroy(n);
  return NULL;
}

int ncselector_additem(ncselector* n, const struct ncselector_item* item){
  unsigned origdimy, origdimx;
  ncselector_dim_yx(n, &origdimy, &origdimx);
  size_t newsize = sizeof(*n->items) * (n->itemcount + 1);
  struct ncselector_int* items = realloc(n->items, newsize);
  if(!items){
    return -1;
  }
  n->items = items;
  n->items[n->itemcount].option = strdup(item->option);
  const char *desc = item->desc ? item->desc : "";
  n->items[n->itemcount].desc = strdup(desc);
  int usafecols = ncstrwidth(item->option, NULL, NULL);
  if(usafecols < 0){
    return -1;
  }
  unsigned cols = usafecols;
  n->items[n->itemcount].opcolumns = cols;
  if(cols > n->longop){
    n->longop = cols;
  }
  cols = ncstrwidth(desc, NULL, NULL);
  n->items[n->itemcount].desccolumns = cols;
  if(cols > n->longdesc){
    n->longdesc = cols;
  }
  ++n->itemcount;
  unsigned dimy, dimx;
  ncselector_dim_yx(n, &dimy, &dimx);
  if(origdimx < dimx || origdimy < dimy){ // resize if too small
    ncplane_resize_simple(n->ncp, dimy, dimx);
  }
  return ncselector_draw(n);
}

int ncselector_delitem(ncselector* n, const char* item){
  unsigned origdimy, origdimx;
  ncselector_dim_yx(n, &origdimy, &origdimx);
  bool found = false;
  int maxop = 0, maxdesc = 0;
  for(unsigned idx = 0 ; idx < n->itemcount ; ++idx){
    if(strcmp(n->items[idx].option, item) == 0){ // found it
      free(n->items[idx].option);
      free(n->items[idx].desc);
      if(idx < n->itemcount - 1){
        memmove(n->items + idx, n->items + idx + 1, sizeof(*n->items) * (n->itemcount - idx - 1));
      }else{
        if(idx){
          --n->selected;
        }
      }
      --n->itemcount;
      found = true;
      --idx;
    }else{
      int cols = ncstrwidth(n->items[idx].option, NULL, NULL);
      if(cols > maxop){
        maxop = cols;
      }
      cols = ncstrwidth(n->items[idx].desc, NULL, NULL);
      if(cols > maxdesc){
        maxdesc = cols;
      }
    }
  }
  if(found){
    n->longop = maxop;
    n->longdesc = maxdesc;
    unsigned dimy, dimx;
    ncselector_dim_yx(n, &dimy, &dimx);
    if(origdimx > dimx || origdimy > dimy){ // resize if too big
      ncplane_resize_simple(n->ncp, dimy, dimx);
    }
    return ncselector_draw(n);
  }
  return -1; // wasn't found
}

ncplane* ncselector_plane(ncselector* n){
  return n->ncp;
}

const char* ncselector_selected(const ncselector* n){
  if(n->itemcount == 0){
    return NULL;
  }
  return n->items[n->selected].option;
}

const char* ncselector_previtem(ncselector* n){
  const char* ret = NULL;
  if(n->itemcount == 0){
    return ret;
  }
  if(n->selected == n->startdisp){
    if(n->startdisp-- == 0){
      n->startdisp = n->itemcount - 1;
    }
  }
  if(n->selected == 0){
    n->selected = n->itemcount;
  }
  --n->selected;
  ret = n->items[n->selected].option;
  ncselector_draw(n);
  return ret;
}

const char* ncselector_nextitem(ncselector* n){
  const char* ret = NULL;
  if(n->itemcount == 0){
    return NULL;
  }
  unsigned lastdisp = n->startdisp;
  lastdisp += n->maxdisplay && n->maxdisplay < n->itemcount ? n->maxdisplay : n->itemcount;
  --lastdisp;
  lastdisp %= n->itemcount;
  if(lastdisp == n->selected){
    if(++n->startdisp == n->itemcount){
      n->startdisp = 0;
    }
  }
  ++n->selected;
  if(n->selected == n->itemcount){
    n->selected = 0;
  }
  ret = n->items[n->selected].option;
  ncselector_draw(n);
  return ret;
}

bool ncselector_offer_input(ncselector* n, const ncinput* nc){
  const int items_shown = ncplane_dim_y(n->ncp) - 4 - (n->title ? 2 : 0);
  if(nc->id == NCKEY_BUTTON1 && nc->evtype == NCTYPE_RELEASE){
    int y = nc->y, x = nc->x;
    if(!ncplane_translate_abs(n->ncp, &y, &x)){
      return false;
    }
    if(y == n->uarrowy && x == n->arrowx){
      ncselector_previtem(n);
      return true;
    }else if(y == n->darrowy && x == n->arrowx){
      ncselector_nextitem(n);
      return true;
    }else if(n->uarrowy < y && y < n->darrowy){
      // FIXME we probably only want to consider it a click if both the release
      // and the depress happened to be on us. for now, just check release.
      // FIXME verify that we're within the body walls!
      // FIXME verify we're on the left of the split?
      // FIXME verify that we're on a visible glyph?
      int cury = (n->selected + n->itemcount - n->startdisp) % n->itemcount;
      int click = y - n->uarrowy - 1;
      while(click > cury){
        ncselector_nextitem(n);
        ++cury;
      }
      while(click < cury){
        ncselector_previtem(n);
        --cury;
      }
      return true;
    }
  }else if(nc->evtype != NCTYPE_RELEASE){
    if(nc->id == NCKEY_UP){
      ncselector_previtem(n);
      return true;
    }else if(nc->id == NCKEY_DOWN){
      ncselector_nextitem(n);
      return true;
    }else if(nc->id == NCKEY_SCROLL_UP){
      ncselector_previtem(n);
      return true;
    }else if(nc->id == NCKEY_SCROLL_DOWN){
      ncselector_nextitem(n);
      return true;
    }else if(nc->id == NCKEY_PGDOWN){
      if(items_shown > 0){
        for(int i = 0 ; i < items_shown ; ++i){
          ncselector_nextitem(n);
        }
      }
      return true;
    }else if(nc->id == NCKEY_PGUP){
      if(items_shown > 0){
        for(int i = 0 ; i < items_shown ; ++i){
          ncselector_previtem(n);
        }
      }
      return true;
    }
  }
  return false;
}

ncplane* ncmultiselector_plane(ncmultiselector* n){
  return n->ncp;
}

// ideal body width given the ncselector's items and secondary/footer
static unsigned
ncmultiselector_body_width(const ncmultiselector* n){
  unsigned cols = 0;
  // the body is the maximum of
  //  * longop + longdesc + 5
  //  * secondary + 2
  //  * footer + 2
  if(n->footercols + 2 > cols){
    cols = n->footercols + 2;
  }
  if(n->secondarycols + 2 > cols){
    cols = n->secondarycols + 2;
  }
  if(n->longitem + 7 > cols){
    cols = n->longitem + 7;
  }
  return cols;
}

// redraw the multiselector widget in its entirety
static int
ncmultiselector_draw(ncmultiselector* n){
  ncplane_erase(n->ncp);
  nccell transchar = NCCELL_TRIVIAL_INITIALIZER;
  nccell_set_fg_alpha(&transchar, NCALPHA_TRANSPARENT);
  nccell_set_bg_alpha(&transchar, NCALPHA_TRANSPARENT);
  // if we have a title, we'll draw a riser. the riser is two rows tall, and
  // exactly four columns longer than the title, and aligned to the right. we
  // draw a rounded box. the body will blow part or all of the bottom away.
  unsigned yoff = 0;
  if(n->title){
    size_t riserwidth = n->titlecols + 4;
    int offx = ncplane_halign(n->ncp, NCALIGN_RIGHT, riserwidth);
    ncplane_cursor_move_yx(n->ncp, 0, 0);
    if(offx){
      ncplane_hline(n->ncp, &transchar, offx);
    }
    ncplane_rounded_box_sized(n->ncp, 0, n->boxchannels, 3, riserwidth, 0);
    n->ncp->channels = n->titlechannels;
    ncplane_printf_yx(n->ncp, 1, offx + 1, " %s ", n->title);
    yoff += 2;
    ncplane_cursor_move_yx(n->ncp, 1, 0);
    if(offx){
      ncplane_hline(n->ncp, &transchar, offx);
    }
  }
  unsigned bodywidth = ncmultiselector_body_width(n);
  unsigned dimy, dimx;
  ncplane_dim_yx(n->ncp, &dimy, &dimx);
  int xoff = ncplane_halign(n->ncp, NCALIGN_RIGHT, bodywidth);
  if(xoff){
    for(unsigned y = yoff + 1 ; y < dimy ; ++y){
      ncplane_cursor_move_yx(n->ncp, y, 0);
      ncplane_hline(n->ncp, &transchar, xoff);
    }
  }
  ncplane_cursor_move_yx(n->ncp, yoff, xoff);
  ncplane_rounded_box_sized(n->ncp, 0, n->boxchannels, dimy - yoff, bodywidth, 0);
  if(n->title){
    n->ncp->channels = n->boxchannels;
    ncplane_putegc_yx(n->ncp, 2, dimx - 1, "┤", NULL);
    if(bodywidth < dimx){
      ncplane_putegc_yx(n->ncp, 2, dimx - bodywidth, "┬", NULL);
    }
    if((n->titlecols + 4 != dimx) && n->titlecols > n->secondarycols){
      ncplane_putegc_yx(n->ncp, 2, dimx - (n->titlecols + 4), "┴", NULL);
    }
  }
  // There is always at least one space available on the right for the
  // secondary title and footer, but we'd prefer to use a few more if we can.
  if(n->secondary){
    int xloc = bodywidth - (n->secondarycols + 1) + xoff;
    if(n->secondarycols < bodywidth - 2){
      --xloc;
    }
    n->ncp->channels = n->footchannels;
    ncplane_putstr_yx(n->ncp, yoff, xloc, n->secondary);
  }
  if(n->footer){
    int xloc = bodywidth - (n->footercols + 1) + xoff;
    if(n->footercols < bodywidth - 2){
      --xloc;
    }
    n->ncp->channels = n->footchannels;
    ncplane_putstr_yx(n->ncp, dimy - 1, xloc, n->footer);
  }
  // Top line of body (background and possibly up arrow)
  ++yoff;
  ncplane_cursor_move_yx(n->ncp, yoff, xoff + 1);
  for(unsigned i = xoff + 1 ; i < dimx - 1 ; ++i){
    nccell transc = NCCELL_TRIVIAL_INITIALIZER; // fall back to base cell
    ncplane_putc(n->ncp, &transc);
  }
  const int bodyoffset = dimx - bodywidth + 2;
  if(n->maxdisplay && n->maxdisplay < n->itemcount){
    n->ncp->channels = n->descchannels;
    n->arrowx = bodyoffset + 1;
    ncplane_putegc_yx(n->ncp, yoff, n->arrowx, "↑", NULL);
  }else{
    n->arrowx = -1;
  }
  n->uarrowy = yoff;
  unsigned printidx = n->startdisp;
  unsigned printed = 0;
  // visible option lines
  for(yoff += 1 ; yoff < dimy - 2 ; ++yoff){
    if(n->maxdisplay && printed == n->maxdisplay){
      break;
    }
    ncplane_cursor_move_yx(n->ncp, yoff, xoff + 1);
    for(unsigned i = xoff + 1 ; i < dimx - 1 ; ++i){
      nccell transc = NCCELL_TRIVIAL_INITIALIZER; // fall back to base cell
      ncplane_putc(n->ncp, &transc);
    }
    n->ncp->channels = n->descchannels;
    if(printidx == n->current){
      n->ncp->channels = (uint64_t)ncchannels_bchannel(n->descchannels) << 32u | ncchannels_fchannel(n->descchannels);
    }
    if(notcurses_canutf8(ncplane_notcurses(n->ncp))){
      ncplane_putegc_yx(n->ncp, yoff, bodyoffset, n->items[printidx].selected ? "☒" : "☐", NULL);
    }else{
      ncplane_putchar_yx(n->ncp, yoff, bodyoffset, n->items[printidx].selected ? 'X' : '-');
    }
    n->ncp->channels = n->opchannels;
    if(printidx == n->current){
      n->ncp->channels = (uint64_t)ncchannels_bchannel(n->opchannels) << 32u | ncchannels_fchannel(n->opchannels);
    }
    ncplane_printf(n->ncp, " %s ", n->items[printidx].option);
    n->ncp->channels = n->descchannels;
    if(printidx == n->current){
      n->ncp->channels = (uint64_t)ncchannels_bchannel(n->descchannels) << 32u | ncchannels_fchannel(n->descchannels);
    }
    ncplane_printf(n->ncp, "%s", n->items[printidx].desc);
    if(++printidx == n->itemcount){
      printidx = 0;
    }
    ++printed;
  }
  // Bottom line of body (background and possibly down arrow)
  ncplane_cursor_move_yx(n->ncp, yoff, xoff + 1);
  for(unsigned i = xoff + 1 ; i < dimx - 1 ; ++i){
    nccell transc = NCCELL_TRIVIAL_INITIALIZER; // fall back to base cell
    ncplane_putc(n->ncp, &transc);
  }
  if(n->maxdisplay && n->maxdisplay < n->itemcount){
    n->ncp->channels = n->descchannels;
    ncplane_putegc_yx(n->ncp, yoff, n->arrowx, "↓", NULL);
  }
  n->darrowy = yoff;
  return 0;
}

const char* ncmultiselector_previtem(ncmultiselector* n){
  const char* ret = NULL;
  if(n->itemcount == 0){
    return ret;
  }
  if(n->current == n->startdisp){
    if(n->startdisp-- == 0){
      n->startdisp = n->itemcount - 1;
    }
  }
  if(n->current == 0){
    n->current = n->itemcount;
  }
  --n->current;
  ret = n->items[n->current].option;
  ncmultiselector_draw(n);
  return ret;
}

const char* ncmultiselector_nextitem(ncmultiselector* n){
  const char* ret = NULL;
  if(n->itemcount == 0){
    return NULL;
  }
  unsigned lastdisp = n->startdisp;
  lastdisp += n->maxdisplay && n->maxdisplay < n->itemcount ? n->maxdisplay : n->itemcount;
  --lastdisp;
  lastdisp %= n->itemcount;
  if(lastdisp == n->current){
    if(++n->startdisp == n->itemcount){
      n->startdisp = 0;
    }
  }
  ++n->current;
  if(n->current == n->itemcount){
    n->current = 0;
  }
  ret = n->items[n->current].option;
  ncmultiselector_draw(n);
  return ret;
}

bool ncmultiselector_offer_input(ncmultiselector* n, const ncinput* nc){
  const int items_shown = ncplane_dim_y(n->ncp) - 4 - (n->title ? 2 : 0);
  if(nc->id == NCKEY_BUTTON1 && nc->evtype == NCTYPE_RELEASE){
    int y = nc->y, x = nc->x;
    if(!ncplane_translate_abs(n->ncp, &y, &x)){
      return false;
    }
    if(y == n->uarrowy && x == n->arrowx){
      ncmultiselector_previtem(n);
      return true;
    }else if(y == n->darrowy && x == n->arrowx){
      ncmultiselector_nextitem(n);
      return true;
    }else if(n->uarrowy < y && y < n->darrowy){
      // FIXME we probably only want to consider it a click if both the release
      // and the depress happened to be on us. for now, just check release.
      // FIXME verify that we're within the body walls!
      // FIXME verify we're on the left of the split?
      // FIXME verify that we're on a visible glyph?
      int cury = (n->current + n->itemcount - n->startdisp) % n->itemcount;
      int click = y - n->uarrowy - 1;
      while(click > cury){
        ncmultiselector_nextitem(n);
        ++cury;
      }
      while(click < cury){
        ncmultiselector_previtem(n);
        --cury;
      }
      return true;
    }
  }else if(nc->evtype != NCTYPE_RELEASE){
    if(nc->id == ' '){
      n->items[n->current].selected = !n->items[n->current].selected;
      ncmultiselector_draw(n);
      return true;
    }else if(nc->id == NCKEY_UP){
      ncmultiselector_previtem(n);
      return true;
    }else if(nc->id == NCKEY_DOWN){
      ncmultiselector_nextitem(n);
      return true;
    }else if(nc->id == NCKEY_PGDOWN){
      if(items_shown > 0){
        for(int i = 0 ; i < items_shown ; ++i){
          ncmultiselector_nextitem(n);
        }
      }
      return true;
    }else if(nc->id == NCKEY_PGUP){
      if(items_shown > 0){
        for(int i = 0 ; i < items_shown ; ++i){
          ncmultiselector_previtem(n);
        }
      }
      return true;
    }else if(nc->id == NCKEY_SCROLL_UP){
      ncmultiselector_previtem(n);
      return true;
    }else if(nc->id == NCKEY_SCROLL_DOWN){
      ncmultiselector_nextitem(n);
      return true;
    }
  }
  return false;
}

// calculate the necessary dimensions based off properties of the selector and
// the containing plane
static int
ncmultiselector_dim_yx(const ncmultiselector* n, unsigned* ncdimy, unsigned* ncdimx){
  unsigned rows = 0, cols = 0; // desired dimensions
  unsigned dimy, dimx; // dimensions of containing screen
  ncplane_dim_yx(ncplane_parent(n->ncp), &dimy, &dimx);
  if(n->title){ // header adds two rows for riser
    rows += 2;
  }
  // we have a top line, a bottom line, two lines of margin, and must be able
  // to display at least one row beyond that, so require five more
  rows += 5;
  if(rows > dimy){ // insufficient height to display selector
    return -1;
  }
  rows += (!n->maxdisplay || n->maxdisplay > n->itemcount ? n->itemcount : n->maxdisplay) - 1; // rows necessary to display all options
  if(rows > dimy){ // claw excess back
    rows = dimy;
  }
  *ncdimy = rows;
  cols = ncmultiselector_body_width(n);
  // the riser, if it exists, is header + 4. the cols are the max of these two.
  if(n->titlecols + 4 > cols){
    cols = n->titlecols + 4;
  }
  if(cols > dimx){ // insufficient width to display selector
    return -1;
  }
  *ncdimx = cols;
  return 0;
}

ncmultiselector* ncmultiselector_create(ncplane* n, const ncmultiselector_options* opts){
  if(n == notcurses_stdplane(ncplane_notcurses(n))){
    logerror("won't use the standard plane"); // would fail later on resize
    return NULL;
  }
  ncmultiselector_options zeroed = {0};
  if(!opts){
    opts = &zeroed;
  }
  if(opts->flags > 0){
    logwarn("provided unsupported flags %016" PRIx64, opts->flags);
  }
  unsigned itemcount = 0;
  if(opts->items){
    for(const struct ncmselector_item* i = opts->items ; i->option ; ++i){
      ++itemcount;
    }
  }
  ncmultiselector* ns = malloc(sizeof(*ns));
  if(ns == NULL){
    return NULL;
  }
  memset(ns, 0, sizeof(*ns));
  ns->title = opts->title ? strdup(opts->title) : NULL;
  ns->titlecols = opts->title ? ncstrwidth(opts->title, NULL, NULL) : 0;
  ns->secondary = opts->secondary ? strdup(opts->secondary) : NULL;
  ns->secondarycols = opts->secondary ? ncstrwidth(opts->secondary, NULL, NULL) : 0;
  ns->footer = opts->footer ? strdup(opts->footer) : NULL;
  ns->footercols = opts->footer ? ncstrwidth(opts->footer, NULL, NULL) : 0;
  ns->current = 0;
  ns->startdisp = 0;
  ns->longitem = 0;
  ns->maxdisplay = opts->maxdisplay;
  ns->opchannels = opts->opchannels;
  ns->boxchannels = opts->boxchannels;
  ns->descchannels = opts->descchannels;
  ns->titlechannels = opts->titlechannels;
  ns->footchannels = opts->footchannels;
  ns->boxchannels = opts->boxchannels;
  ns->darrowy = ns->uarrowy = ns->arrowx = -1;
  if(itemcount){
    if(!(ns->items = malloc(sizeof(*ns->items) * itemcount))){
      goto freeitems;
    }
  }else{
    ns->items = NULL;
  }
  for(ns->itemcount = 0 ; ns->itemcount < itemcount ; ++ns->itemcount){
    const struct ncmselector_item* src = &opts->items[ns->itemcount];
    int unsafe = ncstrwidth(src->option, NULL, NULL);
    if(unsafe < 0){
      goto freeitems;
    }
    unsigned cols = unsafe;
    if(cols > ns->longitem){
      ns->longitem = cols;
    }
    unsafe = ncstrwidth(src->desc, NULL, NULL);
    if(unsafe < 0){
      goto freeitems;
    }
    unsigned cols2 = unsafe;
    if(cols + cols2 > ns->longitem){
      ns->longitem = cols + cols2;
    }
    ns->items[ns->itemcount].option = strdup(src->option);
    ns->items[ns->itemcount].desc = strdup(src->desc);
    ns->items[ns->itemcount].selected = src->selected;
    if(!(ns->items[ns->itemcount].desc && ns->items[ns->itemcount].option)){
      free(ns->items[ns->itemcount].option);
      free(ns->items[ns->itemcount].desc);
      goto freeitems;
    }
  }
  unsigned dimy, dimx;
  ns->ncp = n;
  if(ncmultiselector_dim_yx(ns, &dimy, &dimx)){
    goto freeitems;
  }
  if(ncplane_resize_simple(ns->ncp, dimy, dimx)){
    goto freeitems;
  }
  if(ncplane_set_widget(ns->ncp, ns, (void(*)(void*))ncmultiselector_destroy)){
    goto freeitems;
  }
  ncmultiselector_draw(ns); // deal with error here?
  return ns;

freeitems:
  while(ns->itemcount--){
    free(ns->items[ns->itemcount].option);
    free(ns->items[ns->itemcount].desc);
  }
  free(ns->items);
  free(ns->title); free(ns->secondary); free(ns->footer);
  free(ns);
  ncplane_destroy(n);
  return NULL;
}

void ncmultiselector_destroy(ncmultiselector* n){
  if(n){
    while(n->itemcount--){
      free(n->items[n->itemcount].option);
      free(n->items[n->itemcount].desc);
    }
    if(ncplane_set_widget(n->ncp, NULL, NULL) == 0){
      ncplane_destroy(n->ncp);
    }
    free(n->items);
    free(n->title);
    free(n->secondary);
    free(n->footer);
    free(n);
  }
}

int ncmultiselector_selected(ncmultiselector* n, bool* selected, unsigned count){
  if(n->itemcount != count || n->itemcount < 1){
    return -1;
  }
  while(--count){
    selected[count] = n->items[count].selected;
  }
  return 0;
}
