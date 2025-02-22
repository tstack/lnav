#ifndef NOTCURSES_INTERNAL
#define NOTCURSES_INTERNAL

#ifdef __cplusplus
extern "C" {
#endif

// #include "version.h"
// #include "builddef.h"
#include <uniwidth.h>
#include "compat/compat.h"
#include "notcurses/ncport.h"
#include "notcurses/notcurses.h"
#include "notcurses/direct.h"

#ifndef __MINGW32__
#define API __attribute__((visibility("default")))
#else
#define API __declspec(dllexport)
#endif
#define ALLOC __attribute__((malloc)) __attribute__((warn_unused_result))

// KEY_EVENT is defined by both ncurses.h (prior to 6.3) and wincon.h. since we
// don't use either definition, kill it before inclusion of ncurses.h.
#undef KEY_EVENT
#include <term.h>
#include <time.h>
#include <term.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <wctype.h>
#include <pthread.h>
#include <stdbool.h>
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#include <unictype.h>
#ifndef __MINGW32__
#include <langinfo.h>
#endif
#include "lib/termdesc.h"
#include "lib/egcpool.h"
#include "lib/sprite.h"
#include "lib/fbuf.h"
#include "lib/gpm.h"

struct sixelmap;
struct ncvisual_details;

// Was this glyph drawn as part of an ncvisual? If so, we need to honor
// blitter stacking rather than the standard trichannel solver.
#define NC_BLITTERSTACK_MASK  NC_NOBACKGROUND_MASK

// we can't define multipart ncvisual here, because OIIO requires C++ syntax,
// and we can't go throwing C++ syntax into this header. so it goes.

// A plane is memory for some rectilinear virtual window, plus current cursor
// state for that window, and part of a pile. Each pile has a total order along
// its z-axis. Functions update these virtual planes over a series of API
// calls. Eventually, notcurses_render() is called. We then do a depth buffer
// blit of updated cells. A cell is updated if the topmost plane including that
// cell updates it, not simply if any plane updates it.
//
// A plane may be partially or wholly offscreen--this might occur if the
// screen is resized, for example. Offscreen portions will not be rendered.
// Accesses beyond the borders of a panel, however, are errors.
//
// The framebuffer 'fb' is a set of rows. For scrolling, we interpret it as a
// circular buffer of rows. 'logrow' is the index of the row at the logical top
// of the plane. It only changes from 0 if the plane is scrollable.
typedef struct ncplane {
  nccell* fb;            // "framebuffer" of character cells
  int logrow;            // logical top row, starts at 0, add one for each scroll
  unsigned x, y;         // current cursor location within this plane
  // ncplane_yx() etc. use coordinates relative to the plane to which this
  // plane is bound, but absx/absy are always relative to the terminal origin.
  // they must thus be translated by any function which moves a parent plane.
  int absx, absy;        // origin of the plane relative to the pile's origin
                         //  also used as left and top margin on resize by
                         //  ncplane_resize_marginalized()
  unsigned lenx, leny;   // size of the plane, [0..len{x,y}) is addressable
  egcpool pool;          // attached storage pool for UTF-8 EGCs
  uint64_t channels;     // works the same way as cells

  // a notcurses context is made up of piles, each rooted by one or more root
  // planes. each pile has its own (totally ordered) z-axis.
  struct ncpile* pile;   // pile of which we are a part
  struct ncplane* above; // plane above us, NULL if we're on top
  struct ncplane* below; // plane below us, NULL if we're on bottom

  // every plane is bound to some other plane, unless it is a root plane of a
  // pile. a pile has a set of one or more root planes, all of them siblings.
  // root planes are bound to themselves. the standard plane is always a root
  // plane (since it cannot be reparented). a path exists to a root plane.
  struct ncplane* bnext;  // next in the blist
  struct ncplane** bprev; // blist link back to us
  struct ncplane* blist;  // head of list of bound planes
  struct ncplane* boundto;// plane to which we are bound (ourself for roots)

  sprixel* sprite;       // pointer into the sprixel cache
  tament* tam;           // transparency-annihilation sprite matrix

  void* userptr;         // slot for the user to stick some opaque pointer
  int (*resizecb)(struct ncplane*); // callback after parent is resized
  nccell basecell;       // cell written anywhere that fb[i].gcluster == 0
  char* name;            // used only for debugging
  ncalign_e halign;      // relative to parent plane, for automatic realignment
  ncalign_e valign;      // relative to parent plane, for automatic realignment
  uint16_t stylemask;    // same deal as in a cell
  int margin_b, margin_r;// bottom and right margins, stored for resize
  bool scrolling;        // is scrolling enabled? always disabled by default
  bool fixedbound;       // are we fixed relative to the parent's scrolling?
  bool autogrow;         // do we grow to accommodate output?

  // we need to track any widget to which we are bound, so that (1) we don't
  // end up bound to two widgets and (2) we can clean them up on shutdown
  // (assuming they weren't explicitly cleaned up by the client).
  void* widget;            // widget to which we are bound, can be NULL
  void(*wdestruct)(void*); // widget destructor, NULL iff widget is NULL
} ncplane;

// current presentation state of the terminal. it is carried across render
// instances. initialize everything to 0 on a terminal reset / startup.
typedef struct rasterstate {
  // we assemble the encoded (rasterized) output in an fbuf (a portable POSIX
  // memstream, basically), and keep it around between uses. this could be a
  // problem if it ever tremendously spiked, but that seems unlikely?
  fbuf f;         // buffer for preparing raster glyph/escape stream

  // the current cursor position. this is independent of whether the cursor is
  // visible. it is the cell at which the next write will take place. this is
  // modified by: output, cursor moves, clearing the screen (during refresh).
  int y, x;

  const ncplane* lastsrcp; // last source plane (we emit hpa on plane changes)

  unsigned lastr;   // foreground rgb, overloaded for palindexed fg
  unsigned lastg;
  unsigned lastb;
  unsigned lastbr;  // background rgb, overloaded for palindexed bg
  unsigned lastbg;
  unsigned lastbb;

  // used in CLI mode, these track the end of logical output, to place the
  // cursor following each rasterization. they are tracked thusly:
  //  * initialized to the initial physical cursor position
  //  * when we write to a physical row greater than logendy, update both
  //  * when we scroll, subtract one from logendy
  //   * if logendy reaches -1, reset both to 0
  int logendy, logendx;

  uint16_t curattr; // current attributes set (does not include colors)
  // we elide a color escape iff the color has not changed between two cells
  bool fgelidable;
  bool bgelidable;
  bool fgpalelidable;
  bool bgpalelidable;
  bool fgdefelidable;
  bool bgdefelidable;
} rasterstate;

// Tablets are the toplevel entitites within an ncreel. Each corresponds to
// a single, distinct ncplane.
typedef struct nctablet {
  ncplane* p;                  // border plane, NULL when offscreen
  ncplane* cbp;                // data plane, NULL when offscreen
  struct nctablet* next;
  struct nctablet* prev;
  tabletcb cbfxn;              // application callback to draw cbp
  void* curry;                 // application data provided to cbfxn
} nctablet;

typedef struct ncreel {
  ncplane* p;           // ncplane this ncreel occupies, under tablets
  // doubly-linked list, a circular one when infinity scrolling is in effect.
  // points at the focused tablet (when at least one tablet exists, one must be
  // focused). it will be visibly focused following the next redraw.
  nctablet* tablets;
  nctablet* vft;        // the visibly-focused tablet
  enum {
    LASTDIRECTION_UP,
    LASTDIRECTION_DOWN,
  } direction;          // last direction of travel
  int tabletcount;      // could be derived, but we keep it o(1)
  ncreel_options ropts; // copied in ncreel_create()
} ncreel;

typedef struct ncfdplane {
  ncfdplane_callback cb;      // invoked with fresh hot data
  ncfdplane_done_cb donecb;   // invoked on EOF (if !follow) or error
  void* curry;                // passed to the callbacks
  int fd;                     // we take ownership of the fd, and close it
  bool follow;                // keep trying to read past the end (event-based)
  ncplane* ncp;               // bound ncplane
  pthread_t tid;              // thread servicing this i/o
  bool destroyed;             // set in ncfdplane_destroy() in our own context
} ncfdplane;

typedef struct ncsubproc {
  ncfdplane* nfp;
  pid_t pid;                  // subprocess
  int pidfd;                  // for signalling/watching the subprocess
  pthread_t waittid;          // wait()ing thread if pidfd is not available
  pthread_mutex_t lock;       // guards waited
  bool waited;                // we've wait()ed on it, don't kill/wait further
} ncsubproc;

typedef struct ncreader {
  ncplane* ncp;               // always owned by ncreader
  uint64_t tchannels;         // channels for input text
  uint32_t tattrs;            // attributes for input text
  ncplane* textarea;          // grows as needed iff scrolling is enabled
  int xproject;               // virtual x location of ncp origin on textarea
  bool horscroll;             // is there horizontal panning?
  bool no_cmd_keys;           // are shortcuts disabled?
  bool manage_cursor;         // enable and place a virtual cursor
} ncreader;

typedef struct ncprogbar {
  ncplane* ncp;
  double progress;          // on the range [0, 1]
  uint32_t ulchannel, urchannel, blchannel, brchannel;
  bool retrograde;
} ncprogbar;

typedef struct nctab {
  struct nctabbed* nt; // The nctabbed this belongs to
  tabcb cb;     // tab callback
  char* name;   // tab name
  int namecols; // tab name width in columns
  void* curry;  // user pointer
  struct nctab* prev;
  struct nctab* next;
} nctab;

// various moving parts within a notcurses context (and the user) might need to
// access the stats object, so throw a lock on it. we don't want the lock in
// the actual structure since (a) it's usually unnecessary and (b) it breaks
// memset() and memcpy().
typedef struct ncsharedstats {
  pthread_mutex_t lock;
  ncstats s;
} ncsharedstats;

typedef struct ncdirect {
  ncpalette palette;         // 256-indexed palette can be used instead of/with RGB
  FILE* ttyfp;               // FILE* for output tty
  tinfo tcache;              // terminfo cache
  uint64_t channels;         // current channels
  uint16_t stylemask;        // current styles
  uint64_t flags;            // copied in ncdirect_init() from param
  ncsharedstats stats;       // stats! not as broadly used as in notcurses
  unsigned eof;              // have we seen EOF on stdin?
} ncdirect;

// Extracellular state for a cell during the render process. There is one
// crender per rendered cell, and they are initialized to all zeroes. Be
// careful with order here; padding can easily enlarge this struct from 40
// to 48 bytes.
struct crender {
  nccell c;         // solution cell
  const ncplane *p; // source of glyph for this cell
  sprixel* sprixel; // bitmap encountered during traversal
  uint32_t hcfg;    // fg channel prior to HIGHCONTRAST (need full channel)
  struct {
    // If the glyph we render is from an ncvisual, and has a transparent or
    // blended background, blitter stacking is in effect. This is a complicated
    // issue, but essentially, imagine a bottom block is rendered with a green
    // bottom and transparent top. on a lower plane, a top block is rendered
    // with a red foreground and blue background. Normally, this would result
    // in a blue top and green bottom, but that's not what we ever wanted --
    // what makes sense is a red top and green bottom. So ncvisual rendering
    // sets bits from CELL_BLITTERSTACK_MASK when rendering a cell with a
    // transparent background. When paint() selects a glyph, it checks for these
    // bits. If they are set, any lower planes with CELL_BLITTERSTACK_MASK set
    // take this into account when solving the background color.
    unsigned blittedquads: 4;
    unsigned damaged: 1; // only used in rasterization
    // if NCALPHA_HIGHCONTRAST is in play, we apply the HSV flip once the
    // background is locked in. set highcontrast to indicate this.
    unsigned highcontrast: 1;
    unsigned fgblends: 8;
    unsigned bgblends: 8;
    // we'll need recalculate the foreground relative to the solved background,
    // and then reapply any foreground shading from above the highcontrast
    // declaration. save the foreground state when we go highcontrast.
    unsigned hcfgblends: 8; // number of foreground blends prior to HIGHCONTRAST
    unsigned sprixeled: 1; // have we passed through a sprixel?
    unsigned p_beats_sprixel: 1; // did we solve for our glyph above the bitmap?
  } s;
};

// a pile is a collection of planes which will be rendered together. piles are
// completely distinct with regards to thread-safety; one can always operate
// concurrently on distinct piles (save rasterizing, of course). material from
// other piles will be blown away whenever a pile is rasterized. no ordering
// is meaningful between planes. when a new one is added, or one is destroyed,
// the pilelock (in struct notcurses) is taken. a pile is destroyed whenever
// its last plane is destroyed or reparented away. a pile contains a totally-
// ordered list of piles (ordered on the z axis) and a directed acyclic forest
// of bound planes, with each root plane rooting a DAG.
//
// the geometries are updated each time the pile is rendered. until a pile is
// rendered, its geometries might be out-of-date with regards to the terminal
// description in tcache. when it is rendered again, the geometries will be
// updated, sprixels will have their TAMs rebuilt (if necessary), and each
// root plane will have its resize callback invoked (possibly invoking its
// bound planes' resize callbacks in turn).
//
// at context start, there is one pile (the standard pile), containing one
// plane (the standard plane). each ncplane holds a pointer to its pile.
typedef struct ncpile {
  ncplane* top;               // topmost plane, never NULL
  ncplane* bottom;            // bottommost plane, never NULL
  ncplane* roots;             // head of root plane list
  struct crender* crender;    // array (rows * cols crender objects)
  struct notcurses* nc;       // notcurses context
  struct ncpile *prev, *next; // circular list pointers
  size_t crenderlen;          // size of crender vector
  unsigned dimy, dimx;        // rows and cols at last render/creation
  unsigned cellpxx, cellpxy;  // cell-pixel geometry at last render/creation
  int scrolls;                // how many real lines need be scrolled at raster
  sprixel* sprixelcache;      // sorted list of sprixels, assembled during paint
} ncpile;

// the standard pile can be reached through ->stdplane.
typedef struct notcurses {
  ncplane* stdplane; // standard plane, covers screen

  // the style state of the terminal is carried across render runs
  rasterstate rstate;

  // we keep a copy of the last rendered frame. this facilitates O(1)
  // notcurses_at_yx() and O(1) damage detection (at the cost of some memory).
  // FIXME why isn't this just an ncplane rather than ~10 different members?
  nccell* lastframe;// last rasterized framebuffer, NULL until first raster
  // the last pile we rasterized. NULL until we've rasterized once. might
  // be invalid due to the pile being destroyed; you are only allowed to
  // evaluate it for equality to the pile being currently rasterized. when
  // we switch piles, we need to clear all displayed sprixels, and
  // invalidate the new pile's, pursuant to their display.
  ncpile* last_pile;
  egcpool pool;   // egcpool for lastframe

  unsigned lfdimx; // dimensions of lastframe, unchanged by screen resize
  unsigned lfdimy; // lfdimx/lfdimy are 0 until first rasterization

  int cursory;    // desired cursor placement according to user.
  int cursorx;    // -1 is don't-care, otherwise moved here after each render.

  ncsharedstats stats;   // some statistics across the lifetime of the context
  ncstats stashed_stats; // retain across a context reset, for closing banner

  FILE* ttyfp;    // FILE* for writing rasterized data
  tinfo tcache;   // terminfo cache
  pthread_mutex_t pilelock; // guards pile list, locks resize in render

  // desired margins (best-effort only), copied in from notcurses_options
  int margin_t, margin_b, margin_r, margin_l;
  int loglevel;
  ncpalette palette; // 256-indexed palette can be used instead of/with RGB
  bool palette_damage[NCPALETTESIZE];
  bool touched_palette; // have we ever changed a palette entry?
  uint64_t flags;  // copied from notcurses_options
} notcurses;

typedef struct blitterargs {
  // FIXME begy/begx are really only of interest to scaling; they ought be
  // consumed there, and blitters ought always work with the scaled output.
  int begy;            // upper left start within visual
  int begx;
  int leny;            // number of source pixels to use
  int lenx;
  uint64_t flags;      // flags (as selected from ncvisual_options->flags)
  uint32_t transcolor; // if non-zero, treat the lower 24 bits as a transparent color
  union { // cell vs pixel-specific arguments
    struct {
      int placey;      // placement within ncplane
      int placex;
    } cell;            // for cells
    struct {
      int colorregs;   // number of color registers
      sprixel* spx;    // sprixel object
      int pxoffy;      // pixel y-offset within origin cell
      int pxoffx;      // pixel x-offset within origin cell
      int cellpxy;     // targeted cell-pixel geometry. this ought come from the
      int cellpxx;     // target ncpile, or tcache in Direct Mode
    } pixel;           // for pixels
  } u;
} blitterargs;

// a system for rendering RGBA pixels as text glyphs or sixel/kitty bitmaps
struct blitset {
  ncblitter_e geom;
  unsigned width;   // number of input pixels per output cell, width
  unsigned height;  // number of input pixels per output cell, height
  // the EGCs which form the blitter. bits grow left to right, and then top to
  // bottom. the first character is always a space, the last a full block.
  const wchar_t* egcs;
  // the EGCs which form the various levels of a given plotset. if the geometry
  // is wide, things are arranged with the rightmost side increasing most
  // quickly, i.e. it can be indexed as height arrays of 1 + height glyphs. i.e.
  // the first five braille EGCs are all 0 on the left, [0..4] on the right.
  const wchar_t* plotegcs;
  ncblitter blit;
  const char* name;
  bool fill;
};

#include "blitset.h"

void reset_stats(ncstats* stats);
void summarize_stats(notcurses* nc);

void update_raster_stats(const struct timespec* time1, const struct timespec* time0, ncstats* stats);
void update_render_stats(const struct timespec* time1, const struct timespec* time0, ncstats* stats);
void update_raster_bytes(ncstats* stats, int bytes);
void update_write_stats(const struct timespec* time1, const struct timespec* time0, ncstats* stats, int bytes);

void sigwinch_handler(int signo);

void init_lang(void);

int reset_term_attributes(const tinfo* ti, fbuf* f);
int reset_term_palette(const tinfo* ti, fbuf* f, unsigned touchedpalette);

// if there were missing elements we wanted from terminfo, bitch about them here
void warn_terminfo(const notcurses* nc, const tinfo* ti);

int resize_callbacks_children(ncplane* n);

static inline ncpile*
ncplane_pile(const ncplane* n){
  return n->pile;
}

static inline const ncpile*
ncplane_pile_const(const ncplane* n){
  return n->pile;
}

static inline ncplane*
ncplane_stdplane(ncplane* n){
  return notcurses_stdplane(ncplane_notcurses(n));
}

static inline const ncplane*
ncplane_stdplane_const(const ncplane* n){
  return notcurses_stdplane_const(ncplane_notcurses_const(n));
}

// set the plane's widget and wdestruct fields, returning non-zero if they're
// already non-NULL (i.e. if the plane is already bound), unless we pass NULL
// (which ought be done from the widget destructor, to avoid corecursion).
static inline int
ncplane_set_widget(ncplane* n, void* w, void(*wdestruct)(void*)){
  if(n->widget){
    if(w){
      logerror("plane is already bound to a widget");
      return -1;
    }
  }else if(w == NULL){
    return -1;
  }
  n->widget = w;
  n->wdestruct = wdestruct;
  return 0;
}

// initialize visualization backend (ffmpeg/oiio)
int ncvisual_init(int loglevel);

static inline int
fbcellidx(int row, int rowlen, int col){
//fprintf(stderr, "row: %d rowlen: %d col: %d\n", row, rowlen, col);
  return row * rowlen + col;
}

// take a logical 'y' and convert it to the virtual 'y'. see HACKING.md.
static inline int
logical_to_virtual(const ncplane* n, int y){
//fprintf(stderr, "y: %d n->logrow: %d n->leny: %d\n", y, n->logrow, n->leny);
  return (y + n->logrow) % n->leny;
}

int clear_and_home(notcurses* nc, tinfo* ti, fbuf* f);

static inline int
nfbcellidx(const ncplane* n, int row, int col){
  return fbcellidx(logical_to_virtual(n, row), n->lenx, col);
}

// is the rgb value greyish? note that pure white and pure black are both
// considered greyish according to the definition of this function =].
static inline bool
rgb_greyish_p(unsigned r, unsigned g, unsigned b){
  const unsigned GREYMASK = 0xf8;
  if((r & GREYMASK) == (g & GREYMASK) && (g & GREYMASK) == (b & GREYMASK)){
    return true;
  }
  return false;
}

// For our first attempt, O(1) uniform conversion from 8-bit r/g/b down to
// ~2.4-bit 6x6x6 cube + greyscale (assumed on entry; I know no way to
// even semi-portably recover the palette) proceeds via: map each 8-bit to
// a 5-bit target grey. if all 3 components match, select that grey.
// otherwise, c / 42.7 to map to 6 values.
static inline int
rgb_quantize_256(unsigned r, unsigned g, unsigned b){
  // if all 5 MSBs match, return grey from 24-member grey ramp or pure
  // black/white from original 16 (0 and 15, respectively)
  if(rgb_greyish_p(r, g, b)){
    // 8 and 238 come from https://terminalguide.namepad.de/attr/fgcol256/,
    // which suggests 10-nit intervals otherwise.
    if(r < 8){
      return 0;
    }else if(r > 238){
      return 15;
    }
    return 232 + (r - 8) / 10;
  }
  r /= 43;
  g /= 43;
  b /= 43;
  return r * 36 + g * 6 + b + 16;
}

// We get black, red, green, yellow, blue, magenta, cyan, and white. Ugh. Under
// an additive model: G + R -> Y, G + B -> C, R + B -> M, R + G + B -> W
static inline int
rgb_quantize_8(unsigned r, unsigned g, unsigned b){
  static const int BLACK = 0;
  static const int RED = 1;
  static const int GREEN = 2;
  static const int YELLOW = 3;
  static const int BLUE = 4;
  static const int MAGENTA = 5;
  static const int CYAN = 6;
  static const int WHITE = 7;
  if(rgb_greyish_p(r, g, b)){
    if(r < 64){
      return BLACK;
    }
    return WHITE;
  }
  if(r < 128){ // we have no red
    if(g < 128){ // we have no green
      if(b < 128){
        return BLACK;
      }
      return BLUE;
    } // we have green
    if(b < 128){
      return GREEN;
    }
    return CYAN;
  }else if(g < 128){ // we have red, but not green
    if(b < 128){
      return RED;
    }
    return MAGENTA;
  }else if(b < 128){ // we have red and green
    return YELLOW;
  }
  return WHITE;
}

// Given r, g, and b values 0..255, do a weighted average per Rec. 601, and
// return the 8-bit greyscale value (this value will be the r, g, and b value
// for the new color).
static inline int
rgb_greyscale(int r, int g, int b){
  if(r < 0 || r > 255){
    return -1;
  }
  if(g < 0 || g > 255){
    return -1;
  }
  if(b < 0 || b > 255){
    return -1;
  }
  // Use Rec. 601 scaling plus linear approximation of gamma decompression
  float fg = (0.299 * (r / 255.0) + 0.587 * (g / 255.0) + 0.114 * (b / 255.0));
  return fg * 255;
}

static inline const char*
pool_extended_gcluster(const egcpool* pool, const nccell* c){
  if(cell_simple_p(c)){
    return (const char*)&c->gcluster;
  }
  return egcpool_extended_gcluster(pool, c);
}

static inline nccell*
ncplane_cell_ref_yx(const ncplane* n, unsigned y, unsigned x){
  return &n->fb[nfbcellidx(n, y, x)];
}

static inline void
cell_debug(const egcpool* p, const nccell* c){
  fprintf(stderr, "gcluster: %08x %s style: 0x%04x chan: 0x%016" PRIx64 "\n",
          c->gcluster, egcpool_extended_gcluster(p, c), c->stylemask,
          c->channels);
}

static inline void
plane_debug(const ncplane* n, bool details){
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  fprintf(stderr, "p: %p dim: %d/%d poolsize: %d\n", n, dimy, dimx, n->pool.poolsize);
  if(details){
    for(unsigned y = 0 ; y < 1 ; ++y){
      for(unsigned x = 0 ; x < 10 ; ++x){
        const nccell* c = &n->fb[fbcellidx(y, dimx, x)];
        fprintf(stderr, "[%03d/%03d] ", y, x);
        cell_debug(&n->pool, c);
      }
    }
  }
}

static inline notcurses*
ncpile_notcurses(ncpile* p){
  return p->nc;
}

static inline const notcurses*
ncpile_notcurses_const(const ncpile* p){
  return p->nc;
}

static inline void
ncpile_debug(const ncpile* p, fbuf* f){
  fbuf_printf(f, "  ************************* %16p pile ****************************\n", p);
  const ncplane* n = p->top;
  const ncplane* prev = NULL;
  int planeidx = 0;
  while(n){
    fbuf_printf(f, "%04d off y: %3d x: %3d geom y: %3u x: %3u curs y: %3u x: %3u %p %.4s\n",
                planeidx, n->absy, n->absx, n->leny, n->lenx, n->y, n->x, n, n->name);
    if(n->boundto || n->bnext || n->bprev || n->blist){
      fbuf_printf(f, " bound %p %s %p %s %p binds %p\n",
                  n->boundto, notcurses_canutf8(ncpile_notcurses_const(p)) ? "←" : "<",
                  n->bprev, notcurses_canutf8(ncpile_notcurses_const(p)) ? "→" : ">",
                  n->bnext, n->blist);
    }
    if(n->bprev && (*n->bprev != n)){
      fbuf_printf(f, " WARNING: expected *->bprev %p, got %p\n", n, *n->bprev);
    }
    if(n->above != prev){
      fbuf_printf(f, " WARNING: expected ->above %p, got %p\n", prev, n->above);
    }
    if(ncplane_pile_const(n) != p){
      fbuf_printf(f, " WARNING: expected pile %p, got %p\n", p, ncplane_pile_const(n));
    }
    prev = n;
    n = n->below;
    ++planeidx;
  }
  if(p->bottom != prev){
    fbuf_printf(f, " WARNING: expected ->bottom %p, got %p\n", prev, p->bottom);
  }
}

static inline void
notcurses_debug_fbuf(const notcurses* nc, fbuf* f){
  const ncpile* p = ncplane_pile(nc->stdplane);
  fbuf_printf(f, " -------------------------- notcurses debug state -----------------------------\n");
  const ncpile* p0 = p;
  do{
    ncpile_debug(p0, f);
    const ncpile* prev = p0;
    p0 = p0->next;
    if(p0->prev != prev){
      fbuf_printf(f, "WARNING: expected ->prev %p, got %p\n", prev, p0->prev);
    }
  }while(p != p0);
  fbuf_printf(f, " ______________________________________________________________________________\n");
}

// cell coordinates *within the sprixel*, not absolute
int sprite_wipe(const notcurses* nc, sprixel* s, int y, int x);
void sprixel_free(sprixel* s);
void sprixel_hide(sprixel* s);

// dimy and dimx are cell geometry, not pixel.
sprixel* sprixel_alloc(ncplane* n, int dimy, int dimx);
sprixel* sprixel_recycle(ncplane* n);
int sprite_clear_all(const tinfo* t, fbuf* f);
// these three all use absolute coordinates
void sprixel_invalidate(sprixel* s, int y, int x);
void sprixel_movefrom(sprixel* s, int y, int x);
void sprixel_debug(const sprixel* s, FILE* out);
void sixelmap_free(struct sixelmap *s);

// update any necessary cells underneath the sprixel pursuant to its removal.
// for sixel, this *achieves* the removal, and is performed on every cell.
// returns 1 if the graphic can be immediately freed (which is equivalent to
// asking whether it was sixel and there were no errors).
static inline int
sprite_scrub(const notcurses* n, const ncpile* p, sprixel* s){
//sprixel_debug(s, stderr);
  logdebug("sprixel %u state %d", s->id, s->invalidated);
  return n->tcache.pixel_scrub(p, s);
}

// precondition: s->invalidated is SPRIXEL_INVALIDATED or SPRIXEL_MOVED.
// returns -1 on error, or the number of bytes written.
static inline int
sprite_draw(const tinfo* ti, const ncpile* p, sprixel* s, fbuf* f,
            int yoff, int xoff){
  if(!ti->pixel_draw){
    return 0;
  }
//sprixel_debug(s, stderr);
  logdebug("sprixel %u state %d", s->id, s->invalidated);
  return ti->pixel_draw(ti, p, s, f, yoff, xoff);
}

// precondition: s->invalidated is SPRIXEL_MOVED or SPRIXEL_INVALIDATED
// returns -1 on error, or the number of bytes written.
static inline int
sprite_redraw(notcurses* nc, const ncpile* p, sprixel* s, fbuf* f, int y, int x){
//sprixel_debug(s, stderr);
  const tinfo* ti = &nc->tcache;
  logdebug("sprixel %u state %d", s->id, s->invalidated);
  if(s->invalidated == SPRIXEL_MOVED && ti->pixel_move){
    // if we are kitty prior to 0.20.0, C=1 isn't available to us, and we must
    // not emit it. we use sixel_maxy_pristine as a side channel to encode
    // this version information.
    bool noscroll = !ti->sixel_maxy_pristine;
    return ti->pixel_move(s, f, noscroll, y, x);
  }else{
    if(!ti->pixel_draw){
      return 0;
    }
    int r = ti->pixel_draw(ti, p, s, f, y, x);
    // different terminals put the cursor at different places following
    // emission of a bitmap graphic. just reset y/x.
    nc->rstate.y = -1;
    nc->rstate.x = -1;
    return r;
  }
}

// present a loaded graphic. only defined for kitty.
static inline int
sprite_commit(tinfo* ti, fbuf* f, sprixel* s, unsigned forcescroll){
  if(ti->pixel_commit){
    // if we are kitty prior to 0.20.0, C=1 isn't available to us, and we must
    // not emit it. we use sixel_maxy_pristine as a side channel to encode
    // this version information. direct mode, meanwhile, sets forcescroll.
    bool noscroll = !ti->sixel_maxy_pristine && !forcescroll;
    if(ti->pixel_commit(f, s, noscroll) < 0){
      return -1;
    }
  }
  return 0;
}

static inline void
cleanup_tam(tament* tam, int ydim, int xdim){
  for(int y = 0 ; y < ydim ; ++y){
    for(int x = 0 ; x < xdim ; ++x){
      free(tam[y * xdim + x].auxvector);
      tam[y * xdim + x].auxvector = NULL;
    }
  }
}

static inline void
destroy_tam(ncplane* p){
  if(p->tam){
    cleanup_tam(p->tam, p->leny, p->lenx);
    free(p->tam);
    p->tam = NULL;
  }
}

static inline int
sprite_rebuild(const notcurses* nc, sprixel* s, int ycell, int xcell){
  logdebug("rebuilding %d %d/%d", s->id, ycell, xcell);
  const int idx = s->dimx * ycell + xcell;
  int ret = 0;
  // special case the transition back to SPRIXCELL_TRANSPARENT; this can be
  // done in O(1), since the actual glyph needn't change.
  if(s->n->tam[idx].state == SPRIXCELL_ANNIHILATED_TRANS){
    s->n->tam[idx].state = SPRIXCELL_TRANSPARENT;
  }else if(s->n->tam[idx].state == SPRIXCELL_ANNIHILATED){
    uint8_t* auxvec = (uint8_t*)s->n->tam[idx].auxvector;
    assert(auxvec);
    // sets the new state itself
    ret = nc->tcache.pixel_rebuild(s, ycell, xcell, auxvec);
    if(ret > 0){
      free(auxvec);
      s->n->tam[idx].auxvector = NULL;
    }
  }else{
    return 0;
  }
  // don't upset SPRIXEL_MOVED
  if(s->invalidated == SPRIXEL_QUIESCENT){
    if(s->n->tam[idx].state != SPRIXCELL_TRANSPARENT &&
       s->n->tam[idx].state != SPRIXCELL_ANNIHILATED &&
       s->n->tam[idx].state != SPRIXCELL_ANNIHILATED_TRANS){
      s->invalidated = SPRIXEL_INVALIDATED;
    }
  }
  return ret;
}

// |y| and |x| are scaled geometry on input, and clamped scaled geometry on
// output. |outy| is output geometry on output, and unused on input. output
// geometry is derived from scaled geometry and output requirements (that Sixel
// must be a multiple of six pixels tall). output width is always equal to
// scaled width. all are pixels.
// happy fact: common reported values for maximum sixel height are 256, 1024,
// and 4096...not a single goddamn one of which is divisible by six. augh.
static inline void
clamp_to_sixelmax(const tinfo* t, unsigned* y, unsigned* x, unsigned* outy, ncscale_e scaling){
  if(t->sixel_maxy && *y > t->sixel_maxy){
    *y = t->sixel_maxy;
  }
  *outy = *y;
  if(*outy % t->sprixel_scale_height){
    *outy += t->sprixel_scale_height - (*outy % t->sprixel_scale_height);
    // FIXME use closed form
    while(t->sixel_maxy && *outy > t->sixel_maxy){
      *outy -= t->sprixel_scale_height;
    }
    if(scaling == NCSCALE_STRETCH || *y > *outy){
      *y = *outy;
    }
  }
  if(t->sixel_maxx && *x > t->sixel_maxx){
    *x = t->sixel_maxx;
  }
}

// any sprixcell which does not cover the entirety of the underlying cell
// cannot be SPRIXCELL_OPAQUE. this postprocesses the TAM, flipping any
// such sprixcells to SPRIXCELL_MIXED. |leny| and |lenx| are output geometry
// in pixels. |cdimy| and |cdimx| are output coverage in cells.
static inline void
scrub_tam_boundaries(tament* tam, int leny, int lenx, int cdimy, int cdimx){
  // any sprixcells which don't cover the full cell underneath them cannot
  // be SPRIXCELL_OPAQUE
  const int cols = (lenx + cdimx - 1) / cdimx;
  const int rows = (leny + cdimy - 1) / cdimy;
  if(lenx % cdimx){
    for(int y = 0 ; y < rows ; ++y){
      if(tam[y * cols + cols - 1].state == SPRIXCELL_OPAQUE_KITTY){
        tam[y * cols + cols - 1].state = SPRIXCELL_MIXED_KITTY;
      }else if(tam[y * cols + cols - 1].state == SPRIXCELL_OPAQUE_SIXEL){
        tam[y * cols + cols - 1].state = SPRIXCELL_MIXED_SIXEL;
      }
    }
  }
  if(leny % cdimy){
    const int y = rows - 1;
    for(int x = 0 ; x < cols ; ++x){
      if(tam[y * cols + x].state == SPRIXCELL_OPAQUE_KITTY){
        tam[y * cols + x].state = SPRIXCELL_MIXED_KITTY;
      }else if(tam[y * cols + x].state == SPRIXCELL_OPAQUE_SIXEL){
        tam[y * cols + x].state = SPRIXCELL_MIXED_SIXEL;
      }
    }
  }
}

// get the TAM entry for these (absolute) coordinates
static inline sprixcell_e
sprixel_state(const sprixel* s, int y, int x){
  const ncplane* stdn = notcurses_stdplane_const(ncplane_notcurses_const(s->n));
  int localy = y - (s->n->absy - stdn->absy);
  int localx = x - (s->n->absx - stdn->absx);
//fprintf(stderr, "TAM %%d at %d/%d (%d/%d, %d/%d)\n", /*s->n->tam[localy * s->dimx + localx].state,*/ localy, localx, y, x, s->dimy, s->dimx);
  assert(localy >= 0);
  assert(localy < (int)s->dimy);
  assert(localx >= 0);
  assert(localx < (int)s->dimx);
  return s->n->tam[localy * s->dimx + localx].state;
}

static inline void
pool_release(egcpool* pool, nccell* c){
  if(cell_extended_p(c)){
    egcpool_release(pool, cell_egc_idx(c));
  }
  c->gcluster = 0; // don't subject ourselves to double-release problems
  c->width = 0;    // don't subject ourselves to geometric ambiguities
}

// set the nccell 'c' to point into the egcpool at location 'eoffset'
static inline void
set_gcluster_egc(nccell* c, int eoffset){
  c->gcluster = htole(0x01000000ul) + htole(eoffset);
}

// Duplicate one nccell onto another, possibly crossing ncplanes.
static inline int
cell_duplicate_far(egcpool* tpool, nccell* targ, const ncplane* splane, const nccell* c){
  pool_release(tpool, targ);
  targ->stylemask = c->stylemask;
  targ->channels = c->channels;
  targ->width = c->width;
  if(!cell_extended_p(c)){
    targ->gcluster = c->gcluster;
    return 0;
  }
  const char* egc = nccell_extended_gcluster(splane, c);
  size_t ulen = strlen(egc);
  int eoffset = egcpool_stash(tpool, egc, ulen);
  if(eoffset < 0){
    return -1;
  }
  set_gcluster_egc(targ, eoffset);
  return 0;
}

int ncplane_resize_internal(ncplane* n, int keepy, int keepx,
                            unsigned keepleny, unsigned keeplenx,
                            int yoff, int xoff,
                            unsigned ylen, unsigned xlen);

int update_term_dimensions(unsigned* rows, unsigned* cols, tinfo* tcache, int margin_b,
                           unsigned* cgeo_changed, unsigned* pgeo_changed)
  __attribute__ ((nonnull (3, 5, 6)));

ALLOC static inline void*
memdup(const void* src, size_t len){
  void* ret = malloc(len);
  if(ret){
    memcpy(ret, src, len);
  }
  return ret;
}

ALLOC void* bgra_to_rgba(const void* data, int rows, int* rowstride, int cols, int alpha);
ALLOC void* rgb_loose_to_rgba(const void* data, int rows, int* rowstride, int cols, int alpha);
ALLOC void* rgb_packed_to_rgba(const void* data, int rows, int* rowstride, int cols, int alpha);

// find the "center" cell of two lengths. in the case of even rows/columns, we
// place the center on the top/left. in such a case there will be one more
// cell to the bottom/right of the center.
static inline void
center_box(int* RESTRICT y, int* RESTRICT x){
  if(y){
    *y = (*y - 1) / 2;
  }
  if(x){
    *x = (*x - 1) / 2;
  }
}

// find the "center" cell of a plane. in the case of even rows/columns, we
// place the center on the top/left. in such a case there will be one more
// cell to the bottom/right of the center.
static inline void
ncplane_center(const ncplane* n, int* RESTRICT y, int* RESTRICT x){
  *y = n->leny;
  *x = n->lenx;
  center_box(y, x);
}

int ncvisual_bounding_box(const struct ncvisual* ncv, int* leny, int* lenx,
                          int* offy, int* offx);

// Our gradient is a 2d lerp among the four corners of the region. We start
// with the observation that each corner ought be its exact specified corner,
// and the middle ought be the exact average of all four corners' components.
// Another observation is that if all four corners are the same, every cell
// ought be the exact same color. From this arises the observation that a
// perimeter element is not affected by the other three sides:
//
//  a corner element is defined by itself
//  a perimeter element is defined by the two points on its side
//  an internal element is defined by all four points
//
// 2D equation of state: solve for each quadrant's contribution (min 2x2):
//
//  X' = (xlen - 1) - X
//  Y' = (ylen - 1) - Y
//  TLC: X' * Y' * TL
//  TRC: X * Y' * TR
//  BLC: X' * Y * BL
//  BRC: X * Y * BR
//  steps: (xlen - 1) * (ylen - 1) [maximum steps away from origin]
//
// Then add TLC + TRC + BLC + BRC + steps / 2, and divide by steps (the
//  steps / 2 is to work around truncate-towards-zero).
static int
calc_gradient_component(unsigned tl, unsigned tr, unsigned bl, unsigned br,
                        unsigned y, unsigned x, unsigned ylen, unsigned xlen){
  const int avm = (ylen - 1) - y;
  const int ahm = (xlen - 1) - x;
  if(xlen < 2){
    if(ylen < 2){
      return tl;
    }
    return (tl * avm + bl * y) / (ylen - 1);
  }
  if(ylen < 2){
    return (tl * ahm + tr * x) / (xlen - 1);
  }
  const int tlc = ahm * avm * tl;
  const int blc = ahm * y * bl;
  const int trc = x * avm * tr;
  const int brc = y * x * br;
  const int divisor = (ylen - 1) * (xlen - 1);
  return ((tlc + blc + trc + brc) + divisor / 2) / divisor;
}

// calculate one of the channels of a gradient at a particular point.
static inline uint32_t
calc_gradient_channel(uint32_t ul, uint32_t ur, uint32_t ll, uint32_t lr,
                      unsigned y, unsigned x, unsigned ylen, unsigned xlen){
  uint32_t chan = 0;
  ncchannel_set_rgb8_clipped(&chan,
                           calc_gradient_component(ncchannel_r(ul), ncchannel_r(ur),
                                                   ncchannel_r(ll), ncchannel_r(lr),
                                                   y, x, ylen, xlen),
                           calc_gradient_component(ncchannel_g(ul), ncchannel_g(ur),
                                                   ncchannel_g(ll), ncchannel_g(lr),
                                                   y, x, ylen, xlen),
                           calc_gradient_component(ncchannel_b(ul), ncchannel_b(ur),
                                                   ncchannel_b(ll), ncchannel_b(lr),
                                                   y, x, ylen, xlen));
  ncchannel_set_alpha(&chan, ncchannel_alpha(ul)); // precondition: all αs are equal
  return chan;
}

// calculate both channels of a gradient at a particular point, storing them
// into `channels'. x and y ought be the location within the gradient.
static inline void
calc_gradient_channels(uint64_t* channels, uint64_t ul, uint64_t ur,
                       uint64_t ll, uint64_t lr, unsigned y, unsigned x,
                       unsigned ylen, unsigned xlen){
  if(!ncchannels_fg_default_p(ul)){
    ncchannels_set_fchannel(channels,
                            calc_gradient_channel(ncchannels_fchannel(ul),
                                                  ncchannels_fchannel(ur),
                                                  ncchannels_fchannel(ll),
                                                  ncchannels_fchannel(lr),
                                                  y, x, ylen, xlen));
  }else{
    ncchannels_set_fg_default(channels);
  }
  if(!ncchannels_bg_default_p(ul)){
    ncchannels_set_bchannel(channels,
                            calc_gradient_channel(ncchannels_bchannel(ul),
                                                  ncchannels_bchannel(ur),
                                                  ncchannels_bchannel(ll),
                                                  ncchannels_bchannel(lr),
                                                  y, x, ylen, xlen));
  }else{
    ncchannels_set_bg_default(channels);
  }
}

// ncdirect needs to "fake" an isolated ncplane as a drawing surface for
// ncvisual_blit(), and thus calls these low-level internal functions.
// they are not for general use -- check ncplane_new() and ncplane_destroy().
ncplane* ncplane_new_internal(notcurses* nc, ncplane* n, const ncplane_options* nopts);

void free_plane(ncplane* p);

// heap-allocated formatted output
ALLOC char* ncplane_vprintf_prep(const char* format, va_list ap);

// Resize the provided ncvisual to the specified 'rows' x 'cols', but do not
// change the internals of the ncvisual. Uses oframe.
int ncvisual_blit_internal(const struct ncvisual* ncv, int rows, int cols,
                           ncplane* n, const struct blitset* bset,
                           const blitterargs* bargs);

// if fd < 0, blocking_write() is going to emit an EBADF, so we don't
// bother checking it here explicitly.
static inline int
tty_emit(const char* seq, int fd){
  if(!seq){
    return -1;
  }
  size_t slen = strlen(seq);
  if(blocking_write(fd, seq, slen)){
    return -1;
  }
  return 0;
}

int set_fd_nonblocking(int fd, unsigned state, unsigned* oldstate);

static inline int
term_bg_palindex(const notcurses* nc, fbuf* f, unsigned pal){
  const char* setab = get_escape(&nc->tcache, ESCAPE_SETAB);
  if(setab){
    return fbuf_emit(f, tiparm(setab, pal));
  }
  return 0;
}

static inline int
term_fg_palindex(const notcurses* nc, fbuf* f, unsigned pal){
  const char* setaf = get_escape(&nc->tcache, ESCAPE_SETAF);
  if(setaf){
    return fbuf_emit(f, tiparm(setaf, pal));
  }
  return 0;
}

// check the current and target style bitmasks against the specified 'stylebit'.
// if they are different, and we have the necessary capability, write the
// applicable terminfo entry to 'out'. returns -1 only on a true error.
static int
term_setstyle(fbuf* f, unsigned cur, unsigned targ, unsigned stylebit,
              const char* ton, const char* toff){
  int ret = 0;
  unsigned curon = cur & stylebit;
  unsigned targon = targ & stylebit;
  if(curon != targon){
    if(targon){
      if(ton){
        ret = fbuf_emit(f, ton);
      }
    }else{
      if(toff){ // how did this happen? we can turn it on, but not off?
        ret = fbuf_emit(f, toff);
      }
    }
  }
  if(ret < 0){
    return -1;
  }
  return 0;
}

// emit escapes such that the current style is equal to newstyle. if this
// required an sgr0 (which resets colors), normalized will be non-zero upon
// a successful return.
static inline int
coerce_styles(fbuf* f, const tinfo* ti, uint16_t* curstyle,
              uint16_t newstyle, unsigned* normalized){
  *normalized = 0; // we never currently use sgr0
  int ret = 0;
  ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_BOLD,
                       get_escape(ti, ESCAPE_BOLD), get_escape(ti, ESCAPE_NOBOLD));
  ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_ITALIC,
                       get_escape(ti, ESCAPE_SITM), get_escape(ti, ESCAPE_RITM));
  ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_STRUCK,
                       get_escape(ti, ESCAPE_SMXX), get_escape(ti, ESCAPE_RMXX));
  ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_ALTCHARSET,
                       get_escape(ti, ESCAPE_SMACS), get_escape(ti, ESCAPE_RMACS));
  ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_BLINK,
  get_escape(ti, ESCAPE_BLINK), get_escape(ti, ESCAPE_NOBLINK));
  ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_REVERSE,
                       get_escape(ti, ESCAPE_REVERSE), get_escape(ti, ESCAPE_NOREVERSE));
  // underline and undercurl are exclusive. if we set one, don't go unsetting
  // the other.
  if(newstyle & NCSTYLE_UNDERLINE){ // turn on underline, or do nothing
    ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_UNDERLINE,
                         get_escape(ti, ESCAPE_SMUL), get_escape(ti, ESCAPE_RMUL));
  }else if(newstyle & NCSTYLE_UNDERCURL){ // turn on undercurl, or do nothing
    ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_UNDERCURL,
                         get_escape(ti, ESCAPE_SMULX), get_escape(ti, ESCAPE_SMULNOX));
  }else{ // turn off any underlining
    ret |= term_setstyle(f, *curstyle, newstyle, NCSTYLE_UNDERCURL | NCSTYLE_UNDERLINE,
                         NULL, get_escape(ti, ESCAPE_RMUL));
  }
  *curstyle = newstyle;
  return ret;
}

// DEC private mode set (DECSET) parameters (and corresponding XTerm resources)
#define SET_X10_MOUSE_PROT       "9" // outdated, do not use, use x11
// we can combine 1000--1004, and then use 1005/1006/1015 for extended coordinates
#define SET_X11_MOUSE_PROT    "1000" // for button events
#define SET_HILITE_MOUSE_PROT "1001" // for highlight tracking
#define SET_BTN_EVENT_MOUSE   "1002" // for motion events with buttons
#define SET_ALL_EVENT_MOUSE   "1003" // for motion events without buttons
#define SET_FOCUS_EVENT_MOUSE "1004" // for focus events
#define SET_UTF8_MOUSE_PROT   "1005" // utf8-style extended coordinates
#define SET_SGR_MOUSE_PROT    "1006" // sgr-style extended coordinates
#define SET_ALTERNATE_SCROLL  "1007" // scroll in alternate screen (alternateScroll)
#define SET_TTYOUTPUT_SCROLL  "1010" // scroll on tty output (scrollTtyOutput)
#define SET_KEYPRESS_SCROLL   "1011" // scroll on keypress (scrollKey)
#define SET_URXVT_MOUSE_PROT  "1015" // urxvt-style extended coordinates
#define SET_PIXEL_MOUSE_PROT  "1016" // sgr-style, using pixles rather than cells
#define SET_ENABLE_ALTSCREEN  "1046" // enable alternate screen (*sets* titeInhibit)
#define SET_ALTERNATE_SCREEN  "1047" // replaces 47 (conflict w/DECGRPM) (titeInhibit)
#define SET_SAVE_CURSOR       "1048" // save cursor ala DECSC (titeInhibit)
#define SET_SMCUP             "1049" // 1047+1048 (titeInhibit)
// DECSET/DECRSTs can be chained with semicolons; can we generalize this? FIXME
#define DECSET(p) "\x1b[?" p "h"
#define DECRST(p) "\x1b[?" p "l"

int mouse_setup(tinfo* ti, unsigned eventmask);

// sync the drawing position to the specified location with as little overhead
// as possible (with nothing, if already at the right location). we prefer
// absolute horizontal moves (hpa) to relative ones, in the rare event that
// our understanding of our horizontal location is faulty. if we're moving from
// one plane to another, we emit an hpa no matter what.
// FIXME fall back to synthesized moves in the absence of capabilities (i.e.
// textronix lacks cup; fake it with horiz+vert moves)
// if hardcursorpos is non-zero, we always perform a cup. this is done when we
// don't know where the cursor currently is =].
static inline int
goto_location(notcurses* nc, fbuf* f, int y, int x, const ncplane* srcp){
  int ret = 0;
  // if we don't have hpa, force a cup even if we're only 1 char away. the only
  // TERM i know supporting cup sans hpa is vt100, and vt100 can suck it.
  // you can't use cuf for backwards moves anyway; again, vt100 can suck it.
  const char* hpa = get_escape(&nc->tcache, ESCAPE_HPA);
  // fprintf(stderr, "going to %d/%d from %d/%d hpa: %p\n", y, x, nc->rstate.y, nc->rstate.x, hpa);
  if(nc->rstate.y == y && hpa){ // only need move x
    if(nc->rstate.x == x){
      if(nc->rstate.lastsrcp == srcp || !nc->tcache.gratuitous_hpa){
        return 0; // needn't move shit
      }
      ++nc->stats.s.hpa_gratuitous;
    }
    if(fbuf_emit(f, tiparm(hpa, x))){
      return -1;
    }
  }else{
    // cup is required, no need to verify existence
    const char* cup = get_escape(&nc->tcache, ESCAPE_CUP);
    if(fbuf_emit(f, tiparm(cup, y, x))){
      return -1;
    }
  }
  nc->rstate.x = x;
  nc->rstate.y = y;
  nc->rstate.lastsrcp = srcp;
  return ret;
}

// how many edges need touch a corner for it to be printed?
static inline unsigned
box_corner_needs(unsigned ctlword){
  return (ctlword & NCBOXCORNER_MASK) >> NCBOXCORNER_SHIFT;
}

// True if the cell does not generate background pixels (i.e., the cell is a
// solid or shaded block, or certain emoji).
static inline bool
nccell_nobackground_p(const nccell* c){
  // needs to match all four bits of NC_NOBACKGROUND_MASK, not just one
  return (c->channels & NC_NOBACKGROUND_MASK) == NC_NOBACKGROUND_MASK;
}

// True iff the foreground and background color are both RGB, and equal.
static inline bool
nccell_rgbequal_p(const nccell* c){
  if(nccell_fg_default_p(c) || nccell_fg_palindex_p(c)){
    return false;
  }
  if(nccell_bg_default_p(c) || nccell_bg_palindex_p(c)){
    return false;
  }
  return nccell_fg_rgb(c) == nccell_bg_rgb(c);
}

// Returns a number 0 <= n <= 15 representing the four quadrants, and which (if
// any) are occupied due to blitting with a transparent background. The mapping
// is {tl, tr, bl, br}.
static inline unsigned
cell_blittedquadrants(const nccell* c){
  return ((c->channels & 0x8000000000000000ull) ? 1 : 0) |
         ((c->channels & 0x0400000000000000ull) ? 2 : 0) |
         ((c->channels & 0x0200000000000000ull) ? 4 : 0) |
         ((c->channels & 0x0100000000000000ull) ? 8 : 0);
}

// Set this whenever blitting an ncvisual, when we have a transparent
// background. In such cases, ncvisuals underneath the cell must be rendered
// slightly differently.
static inline void
cell_set_blitquadrants(nccell* c, unsigned tl, unsigned tr, unsigned bl, unsigned br){
  // FIXME want a static assert that these four constants OR together to
  // equal CELL_BLITTERSTACK_MASK, bah
  uint64_t newval = (tl ? 0x8000000000000000ull : 0) |
                    (tr ? 0x0400000000000000ull : 0) |
                    (bl ? 0x0200000000000000ull : 0) |
                    (br ? 0x0100000000000000ull : 0);
  c->channels = ((c->channels & ~NC_BLITTERSTACK_MASK) | newval);
}

// Destroy a plane and all its bound descendants.
int ncplane_destroy_family(ncplane *ncp);

// Extract the 32-bit background channel from a cell.
static inline uint32_t
cell_bchannel(const nccell* cl){
  return ncchannels_bchannel(cl->channels);
}

// Extract those elements of the channel which are common to both foreground
// and background channel representations.
static inline uint32_t
channel_common(uint32_t channel){
  return channel & (NC_BGDEFAULT_MASK | NC_BG_RGB_MASK |
                    NC_BG_PALETTE | NC_BG_ALPHA_MASK);
}

// Extract those elements of the background channel which may be freely swapped
// with the foreground channel (alpha and coloring info).
static inline uint32_t
cell_bchannel_common(const nccell* cl){
  return channel_common(cell_bchannel(cl));
}

// Extract the 32-bit foreground channel from a cell.
static inline uint32_t
cell_fchannel(const nccell* cl){
  return ncchannels_fchannel(cl->channels);
}

// Extract those elements of the foreground channel which may be freely swapped
// with the background channel (alpha and coloring info).
static inline uint32_t
cell_fchannel_common(const nccell* cl){
  return channel_common(cell_fchannel(cl));
}

// Set the 32-bit background channel of an nccell.
static inline uint64_t
cell_set_bchannel(nccell* cl, uint32_t channel){
  return ncchannels_set_bchannel(&cl->channels, channel);
}

// Set the 32-bit foreground channel of an nccell.
static inline uint64_t
cell_set_fchannel(nccell* cl, uint32_t channel){
  return ncchannels_set_fchannel(&cl->channels, channel);
}

// Returns the result of blending two channels. 'blends' indicates how heavily
// 'c1' ought be weighed. If 'blends' is 0 (indicating that 'c1' has not yet
// been set), 'c1' will be entirely determined by 'c2'. Otherwise, the default
// color is preserved if both are the default, palette color is preserved if
// both are the same palette index, and the result is otherwise RGB.
static inline unsigned
channels_blend(notcurses* nc, unsigned c1, unsigned c2, unsigned* blends,
               uint32_t defchan){
  if(ncchannel_alpha(c2) == NCALPHA_TRANSPARENT){
    return c1; // do *not* increment *blends
  }
  if(*blends == 0){
    // don't just return c2, or you set wide status and all kinds of crap
    if(ncchannel_default_p(c2)){
      ncchannel_set_default(&c1);
    }else if(ncchannel_palindex_p(c2)){
      ncchannel_set_palindex(&c1, ncchannel_palindex(c2));
    }else{
      ncchannel_set(&c1, ncchannel_rgb(c2));
    }
  }else if(ncchannel_default_p(c1) && ncchannel_default_p(c2)){
    // do nothing, leave as default
  }else if((ncchannel_palindex_p(c1) && ncchannel_palindex_p(c2)) &&
           ncchannel_palindex(c1) == ncchannel_palindex(c2)){
    // do nothing, leave as palette
  }else{
    unsigned r1, g1, b1;
    unsigned r2, g2, b2;
    if(ncchannel_default_p(c2)){
      ncchannel_rgb8(defchan, &r2, &g2, &b2);
    }else if(ncchannel_palindex_p(c2)){
      ncchannel_rgb8(nc->palette.chans[ncchannel_palindex(c2)],
                     &r2, &g2, &b2);
    }else{
      ncchannel_rgb8(c2, &r2, &g2, &b2);
    }
    if(ncchannel_default_p(c1)){
      ncchannel_rgb8(defchan, &r1, &g1, &b1);
    }else if(ncchannel_palindex_p(c1)){
      ncchannel_rgb8(nc->palette.chans[ncchannel_palindex(c1)],
                     &r1, &g1, &b1);
    }else{
      ncchannel_rgb8(c1, &r1, &g1, &b1);
    }
    unsigned r = (r1 * *blends + r2) / (*blends + 1);
    unsigned g = (g1 * *blends + g2) / (*blends + 1);
    unsigned b = (b1 * *blends + b2) / (*blends + 1);
    ncchannel_set_rgb8(&c1, r, g, b);
  }
  ncchannel_set_alpha(&c1, ncchannel_alpha(c2));
  ++*blends;
  return c1;
}

// do not pass palette-indexed channels!
static inline uint64_t
cell_blend_fchannel(notcurses* nc, nccell* cl, unsigned channel, unsigned* blends){
  return cell_set_fchannel(cl, channels_blend(nc, cell_fchannel(cl),
                           channel, blends, nc->tcache.fg_default));
}

static inline uint64_t
cell_blend_bchannel(notcurses* nc, nccell* cl, unsigned channel, unsigned* blends){
  return cell_set_bchannel(cl, channels_blend(nc, cell_bchannel(cl),
                           channel, blends, nc->tcache.bg_collides_default));
}

// a sprixel occupies the entirety of its associated plane, usually an entirely
// new, purpose-specific plane. |leny| and |lenx| are output geometry in pixels.
static inline int
plane_blit_sixel(sprixel* spx, fbuf* f, int leny, int lenx,
                 int parse_start, tament* tam, sprixel_e state){
  if(sprixel_load(spx, f, leny, lenx, parse_start, state)){
    return -1;
  }
  ncplane* n = spx->n;
  if(n){
//fprintf(stderr, "TAM WAS: %p NOW: %p\n", n->tam, tam);
    if(n->tam != tam){
      destroy_tam(n);
    }
    n->tam = tam;
    n->sprite = spx;
  }
  return 0;
}

// is it a control character? check C0 and C1, but don't count empty strings,
// nor single-byte strings containing only a NUL character.
static inline bool
is_control_egc(const unsigned char* egc, int bytes){
  if(bytes == 1){
    if(*egc < 0x20 || *egc == 0x7f){ // includes NUL terminator
      return true;
    }
  }else if(bytes == 2){
    // 0xc2 followed by 0x80--0x9f are controls. 0xc2 followed by <0x80 is
    // simply invalid utf8.
    if(egc[0] == 0xc2){
      if(egc[1] < 0xa0){
        return true;
      }
    }
  }
  return false;
}

// lowest level of cell+pool setup. if the EGC changes the output to RTL, it
// must be suffixed with a LTR-forcing character by now. The four bits of
// NC_BLITTERSTACK_MASK ought already be initialized. If gcluster is four
// bytes or fewer, this function cannot fail.
static inline int
pool_blit_direct(egcpool* pool, nccell* c, const char* gcluster, int bytes, int cols){
  pool_release(pool, c);
  if(bytes < 0 || cols < 0){
    return -1;
  }
  // we allow newlines and tabs to be blitted into the pool, as they're picked
  // up and given special semantics by output, paint(), and rasterization.
  if(is_control_egc((const unsigned char*)gcluster, bytes) && *gcluster != '\n' && *gcluster != '\t'){
    logerror("not loading control character %u", *(const unsigned char*)gcluster);
    return -1;
  }
  c->width = cols;
  if(bytes <= 4){
    c->gcluster = 0;
    memcpy(&c->gcluster, gcluster, bytes);
  }else{
    int eoffset = egcpool_stash(pool, gcluster, bytes);
    if(eoffset < 0){
      return -1;
    }
    set_gcluster_egc(c, eoffset);
  }
  return bytes;
}

// Reset the quadrant occupancy bits, and pass the cell down to
// pool_blit_direct(). Returns the number of bytes loaded.
static inline int
pool_load_direct(egcpool* pool, nccell* c, const char* gcluster, int bytes, int cols){
  c->channels &= ~NC_NOBACKGROUND_MASK;
  return pool_blit_direct(pool, c, gcluster, bytes, cols);
}

static inline int
cell_load_direct(ncplane* n, nccell* c, const char* gcluster, int bytes, int cols){
  return pool_load_direct(&n->pool, c, gcluster, bytes, cols);
}

// increment y by 1 and rotate the framebuffer up one line. x moves to 0.
void scroll_down(ncplane* n);

static inline bool
islinebreak(wchar_t wchar){
  // UC_LINE_SEPARATOR + UC_PARAGRAPH_SEPARATOR
  if(wchar == L'\n' || wchar == L'\v' || wchar == L'\f'){
    return true;
  }
  const uint32_t mask = UC_CATEGORY_MASK_Zl | UC_CATEGORY_MASK_Zp;
  return uc_is_general_category_withtable(wchar, mask);
}

static inline bool
iswordbreak(wchar_t wchar){
  const uint32_t mask = UC_CATEGORY_MASK_Z |
                        UC_CATEGORY_MASK_Zs;
  return uc_is_general_category_withtable(wchar, mask);
}

// the heart of damage detection. compare two nccells (from two different
// planes) for equality. if they are equal, return 0. otherwise, dup the second
// onto the first and return non-zero.
static inline int
cellcmp_and_dupfar(egcpool* dampool, nccell* damcell,
                   const ncplane* srcplane, const nccell* srccell){
  if(damcell->stylemask == srccell->stylemask){
    if(damcell->channels == srccell->channels){
      const char* srcegc = nccell_extended_gcluster(srcplane, srccell);
      const char* damegc = pool_extended_gcluster(dampool, damcell);
      if(strcmp(damegc, srcegc) == 0){
        return 0; // EGC match
      }
    }
  }
  cell_duplicate_far(dampool, damcell, srcplane, srccell);
  return 1;
}

int get_tty_fd(FILE* ttyfp);

// Given the four channels arguments, verify that:
//
// - if any is default foreground, all are default foreground
// - if any is default background, all are default background
// - all foregrounds must have the same alpha
// - all backgrounds must have the same alpha
// - palette-indexed color must not be used
//
// If you only want to check n < 4 channels, just duplicate one.
bool check_gradient_args(uint64_t ul, uint64_t ur, uint64_t bl, uint64_t br);

// takes a signed starting coordinate (where -1 indicates the cursor's
// position), and an unsigned vector (where 0 indicates "everything
// remaining", i.e. to the right and below). returns 0 iff everything
// is valid and on the plane, filling in 'ystart'/'xstart' with the
// (non-negative) starting coordinates and 'ylen'/'xlen with the
// (positive) dimensions of the affected area.
static inline int
check_geometry_args(const ncplane* n, int y, int x,
                    unsigned* ylen, unsigned* xlen,
                    unsigned* ystart, unsigned* xstart){
  // handle the special -1 case for y/x, and reject other negatives
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
  // y and x are both now definitely positive, but might be off-plane.
  // lock in y and x as ystart and xstart for unsigned comparisons.
  *ystart = y;
  *xstart = x;
  unsigned ymax, xmax;
  ncplane_dim_yx(n, &ymax, &xmax);
  if(*ystart >= ymax || *xstart >= xmax){
    logerror("invalid starting coordinates: %u/%u", *ystart, *xstart);
    return -1;
  }
  // handle the special 0 case for ylen/xlen
  if(*ylen == 0){
    *ylen = ymax - *ystart;
  }
  if(*xlen == 0){
    *xlen = xmax - *xstart;
  }
  // ensure ylen/xlen are on-plane
  if(*ylen > ymax){
    logerror("ylen > dimy %u > %u", *ylen, ymax);
    return -1;
  }
  if(*xlen > xmax){
    logerror("xlen > dimx %u > %u", *xlen, xmax);
    return -1;
  }
  // ensure x + xlen and y + ylen are on-plane, without overflow
  if(ymax - *ylen < *ystart){
    logerror("y + ylen > ymax %u + %u > %u", *ystart, *ylen, ymax);
    return -1;
  }
  if(xmax - *xlen < *xstart){
    logerror("x + xlen > xmax %u + %u > %u", *xstart, *xlen, xmax);
    return -1;
  }
  return 0;
}

void ncvisual_printbanner(fbuf* f);

// alpha comes to us 0--255, but we have only 3 alpha values to map them to
// (opaque, blended, and transparent). it's necessary that we display
// something for any non-zero alpha (see #1540), so the threshold is 1.
// we might want to map this to blended, but we only use opaque and
// transparent for now. if |transcolor| is non-zero, match its lower 24
// bits against each pixel's RGB value, and treat a match as transparent.
static inline bool
rgba_trans_p(uint32_t p, uint32_t transcolor){
  if(ncpixel_a(p) < 192){
    return true;
  }
  if(transcolor &&
      (ncpixel_r(p) == (transcolor & 0xff0000ull) >> 16) &&
      (ncpixel_g(p) == (transcolor & 0xff00ull) >> 8) &&
      (ncpixel_b(p) == (transcolor & 0xffull))){
    return true;
  }
  return false;
}

// get a non-negative "manhattan distance" between two rgb values
static inline uint32_t
rgb_diff(unsigned r1, unsigned g1, unsigned b1, unsigned r2, unsigned g2, unsigned b2){
  uint32_t distance = 0;
  distance += r1 > r2 ? r1 - r2 : r2 - r1;
  distance += g1 > g2 ? g1 - g2 : g2 - g1;
  distance += b1 > b2 ? b1 - b2 : b2 - b1;
//fprintf(stderr, "RGBDIFF %u %u %u %u %u %u: %u\n", r1, g1, b1, r2, g2, b2, distance);
  return distance;
}

// returns non-zero iff the two planes intersect
static inline unsigned
ncplanes_intersect_p(const ncplane* p1, const ncplane* p2){
  int y1, x1, y2, x2;
  int b1, r1, b2, r2;
  ncplane_abs_yx(p1, &y1, &x1);
  b1 = y1 + ncplane_dim_y(p1) - 1;
  r1 = x1 + ncplane_dim_x(p1) - 1;
  ncplane_abs_yx(p2, &y2, &x2);
  b2 = y2 + ncplane_dim_y(p2) - 1;
  r2 = x2 + ncplane_dim_x(p2) - 1;
  if(b1 < y2){ // p1 is above p2, no intersection
    return 0;
  }
  if(b2 < y1){ // p2 is above p1, no intersection
    return 0;
  }
  if(r1 < x2){ // p1 is to the left of p2, no intersection
    return 0;
  }
  if(r2 < x1){ // p2 is to the left of p1, no intersection
    return 0;
  }
  return 1;
}

static inline uint64_t
ncdirect_channels(const ncdirect* nc){
  return nc->channels;
}

static inline bool
ncdirect_fg_default_p(const ncdirect* nc){
  return ncchannels_fg_default_p(ncdirect_channels(nc));
}

static inline bool
ncdirect_bg_default_p(const ncdirect* nc){
  return ncchannels_bg_default_p(ncdirect_channels(nc));
}

static inline bool
ncdirect_fg_palindex_p(const ncdirect* nc){
  return ncchannels_fg_palindex_p(ncdirect_channels(nc));
}

static inline bool
ncdirect_bg_palindex_p(const ncdirect* nc){
  return ncchannels_bg_palindex_p(ncdirect_channels(nc));
}

int ncdirect_set_fg_rgb_f(ncdirect* nc, unsigned rgb, fbuf* f);
int ncdirect_set_bg_rgb_f(ncdirect* nc, unsigned rgb, fbuf* f);
int term_fg_rgb8(const tinfo* ti, fbuf* f, unsigned r, unsigned g, unsigned b);

const struct blitset* lookup_blitset(const tinfo* tcache, ncblitter_e setid, bool may_degrade);

static inline int
rgba_blit_dispatch(ncplane* nc, const struct blitset* bset,
                   int linesize, const void* data,
                   int leny, int lenx, const blitterargs* bargs){
  return bset->blit(nc, linesize, data, leny, lenx, bargs);
}

int ncvisual_geom_inner(const tinfo* ti, const struct ncvisual* n,
                        const struct ncvisual_options* vopts, ncvgeom* geom,
                        const struct blitset** bset,
                        unsigned* disppxy, unsigned* disppxx,
                        unsigned* outy, unsigned* outx,
                        int* placey, int* placex);

static inline const struct blitset*
rgba_blitter_low(const tinfo* tcache, ncscale_e scale, bool maydegrade,
                 ncblitter_e blitrec) {
  if(blitrec == NCBLIT_DEFAULT){
    blitrec = rgba_blitter_default(tcache, scale);
  }
  return lookup_blitset(tcache, blitrec, maydegrade);
}

// RGBA visuals all use NCBLIT_2x1 by default (or NCBLIT_1x1 if not in
// UTF-8 mode), but an alternative can be specified.
static inline const struct blitset*
rgba_blitter(const struct tinfo* tcache, const struct ncvisual_options* opts) {
  const bool maydegrade = !(opts && (opts->flags & NCVISUAL_OPTION_NODEGRADE));
  const ncscale_e scale = opts ? opts->scaling : NCSCALE_NONE;
  return rgba_blitter_low(tcache, scale, maydegrade, opts ? opts->blitter : NCBLIT_DEFAULT);
}

// naive resize of |bmap| from |srows|x|scols| -> |drows|x|dcols|, suitable for
// pixel art. we either select at a constant interval (for shrinking) or duplicate
// at a constant ratio (for inflation). in the absence of a multimedia engine, this
// is the only kind of resizing we support.
static inline uint32_t*
resize_bitmap(const uint32_t* bmap, int srows, int scols, size_t sstride,
              int drows, int dcols, size_t dstride){
  if(sstride < scols * sizeof(*bmap)){
    return NULL;
  }
  if(dstride < dcols * sizeof(*bmap)){
    return NULL;
  }
  // FIXME if parameters match current setup, do nothing, and return bmap
  size_t size = drows * dstride;
  uint32_t* ret = (uint32_t*)malloc(size);
  if(ret == NULL){
    return NULL;
  }
  float xrat = (float)dcols / scols;
  float yrat = (float)drows / srows;
  int dy = 0;
  for(int y = 0 ; y < srows ; ++y){
    float ytarg = (y + 1) * yrat;
    if(ytarg > drows){
      ytarg = drows;
    }
    while(ytarg > dy){
      int dx = 0;
      for(int x = 0 ; x < scols ; ++x){
        float xtarg = (x + 1) * xrat;
        if(xtarg > dcols){
          xtarg = dcols;
        }
        while(xtarg > dx){
          ret[dy * dstride / sizeof(*ret) + dx] = bmap[y * sstride / sizeof(*ret) + x];
          ++dx;
        }
      }
      ++dy;
    }
  }
  return ret;
}

// a neighbor on which to polyfill. by the time we get to it, it might or
// might not have been filled in. if so, discard immediately. otherwise,
// check self, and if valid, push all neighbors.
struct topolyfill {
  int y, x;
  struct topolyfill* next;
};

static inline struct topolyfill*
create_polyfill_op(int y, int x, struct topolyfill** stck){
  // cast for the benefit of c++ callers
  struct topolyfill* n = (struct topolyfill*)malloc(sizeof(*n));
  if(n){
    n->y = y;
    n->x = x;
    n->next = *stck;
    *stck = n;
  }
  return n;
}

// implemented by a multimedia backend (ffmpeg or oiio), and installed
// prior to calling notcurses_core_init() (by notcurses_init()).
typedef struct ncvisual_implementation {
  int (*visual_init)(int loglevel);
  void (*visual_printbanner)(fbuf* f);
  int (*visual_blit)(const struct ncvisual* ncv, unsigned rows, unsigned cols,
                     ncplane* n, const struct blitset* bset, const blitterargs* barg);
  struct ncvisual* (*visual_create)(void);
  struct ncvisual* (*visual_from_file)(const char* fname);
  // ncv constructors other than ncvisual_from_file() need to set up the
  // AVFrame* 'frame' according to their own data, which is assumed to
  // have been prepared already in 'ncv'.
  void (*visual_details_seed)(struct ncvisual* ncv);
  int (*visual_decode)(struct ncvisual* nc);
  int (*visual_decode_loop)(struct ncvisual* nc);
  int (*visual_stream)(notcurses* nc, struct ncvisual* ncv, float timescale,
                       ncstreamcb streamer, const struct ncvisual_options* vopts, void* curry);
  ncplane* (*visual_subtitle)(ncplane* parent, const struct ncvisual* ncv);
  int rowalign; // rowstride base, can be 0 for no padding
  // do a persistent resize, changing the ncv itself
  int (*visual_resize)(struct ncvisual* ncv, unsigned rows, unsigned cols);
  void (*visual_destroy)(struct ncvisual* ncv);
  bool canopen_images;
  bool canopen_videos;
} ncvisual_implementation;

// populated by libnotcurses.so if linked with multimedia
API extern ncvisual_implementation* visual_implementation;

// within unix, we can just use isatty(3). on windows, things work
// differently. for a true Windows Terminal, we'll have HANDLE pointers
// rather than file descriptors. in cygwin/msys2, isatty(3) always fails.
// so for __MINGW32__, always return true. otherwise return isatty(fd).
static inline int
tty_check(int fd){
#ifdef __MINGW32__
  return _isatty(fd);
#endif
  return isatty(fd);
}

// attempt to cancel the specified thread (not an error if we can't; it might
// have already exited), and then join it (an error here is propagated).
static inline int
cancel_and_join(const char* name, pthread_t tid, void** res){
  if(pthread_cancel(tid)){
    logerror("couldn't cancel %s thread", name); // tid might have died
  }
  if(pthread_join(tid, res)){
    logerror("error joining %s thread", name);
    return -1;
  }
  return 0;
}

static inline int
emit_scrolls(const tinfo* ti, int count, fbuf* f){
  logdebug("emitting %d scrolls", count);
  if(count > 1){
    const char* indn = get_escape(ti, ESCAPE_INDN);
    if(indn){
      if(fbuf_emit(f, tiparm(indn, count)) < 0){
        return -1;
      }
      return 0;
    }
  }
  const char* ind = get_escape(ti, ESCAPE_IND);
  if(ind == NULL){
    ind = "\v";
  }
  // fell through if we had no indn
  while(count > 0){
    if(fbuf_emit(f, ind) < 0){
      return -1;
    }
    --count;
  }
  return 0;
}

// both emit the |count > 0| scroll ops to |f|, and update the cursor
// tracking in |nc|
static inline int
emit_scrolls_track(notcurses* nc, int count, fbuf* f){
  if(emit_scrolls(&nc->tcache, count, f)){
    return -1;
  }
  nc->rstate.y -= count;
  nc->rstate.x = 0;
  return 0;
}

// replace or populate the TERM environment variable with 'termname'
int putenv_term(const char* termname) __attribute__ ((nonnull (1)));

// check environment for NOTCURSES_LOGLEVEL, and use it if defined.
int set_loglevel_from_env(ncloglevel_e* loglevel)
  __attribute__ ((nonnull (1)));

// glibc's _nl_normalize_charset() converts to lowercase, removing everything
// but alnums. furthermore, "cs" is a valid prefix meaning "character set".
static inline bool
encoding_is_utf8(const char *enc){
  if(tolower(enc[0]) == 'c' && tolower(enc[1]) == 's'){ // strncasecmp() isn't ansi/iso
    enc += 2; // skip initial "cs" if present.
  }
  const char utfstr[] = "utf8";
  const char* match = utfstr;
  while(*enc){
    if(isalnum(*enc)){ // we only care about alnums
      if(tolower(*enc) != tolower(*match)){
        return false;
      }
      ++match;
    }
    ++enc;
  }
  if(*match){
    return false;
  }
  return true;
}

// tell ncmetric that utf8 is available. should be per-context, but isn't.
void ncmetric_use_utf8(void);

#undef API
#undef ALLOC

#ifdef __cplusplus
}
#endif

#endif
