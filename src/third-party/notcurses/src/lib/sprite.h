#ifndef NOTCURSES_SPRITE
#define NOTCURSES_SPRITE

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "fbuf.h"

#define SIXEL_MAX_REGISTERS 65534 // 65535 is used for transparent

struct tinfo;
struct ncpile;
struct ncplane;
struct sixelmap;
struct blitterargs;

typedef enum {
  SPRIXEL_QUIESCENT,   // up-to-date and visible at the proper place
  SPRIXEL_UNSEEN,      // not yet loaded, invisible, but wants loading
  SPRIXEL_LOADED,      // loaded, but not yet made visible (kitty-only)
  SPRIXEL_INVALIDATED, // not up-to-date, need reload
  SPRIXEL_HIDE,        // queued for destruction
  SPRIXEL_MOVED,       // visible, up-to-date, but in the wrong place
} sprixel_e;

// elements of the T-A matrix describe transparency and annihilation at a
// per-cell basis, making up something of a state machine. when a sprixel
// plane is first created, the TAM is (meaninglessly) initialized to all
// zeroes (SPRIXCELL_OPAQUE). during the construction of the sprixel from
// an RGBA frame, OPAQUE entries are possibly marked MIXED or TRANSPARENT.
// subsequent sprixels blitted to the same plane will reuse the TAM, and
// retain any SPRIXCELL_ANNIHILATED entries, cutting them out of the
// sprixel.
//
// sixel can transition to ANNIHILATED via a no-op; kitty can transition
// to ANNIHILATED only by wiping the cell (removing it from the sprixel via
// all-0 alphas), deleting the bitmap, and displaying it once more. sixel
// bitmaps are removed by obliterating them with new output, while kitty
// bitmaps are removed by a fixed-length terminal escape. an important
// implication is that sixels cannot be progressively reduced by emitting
// progressively more transparent sixels atop one another--to remove a
// cell from a Sixel sprixel, it is necessary to print a glyph. the same
// goes for Kitty sprixels, but there we delete and rerender bitmaps
// in toto without glyph involvement.
//
// a glyph above an OPAQUE sprixel requires annihilating the underlying cell,
// and emitting the glyph only after annihilation is complete. a glyph below
// an OPAQUE sprixel should never be emitted (update the lastframe to
// contain it, but do not mark the cell damaged). should the sprixel be
// removed, the cell will be marked damaged, and the glyph will be updated.
//
// a glyph above a MIXED sprixcell requires the same process as one above an
// OPAQUE sprixcell. a glyph below a MIXED sprixcell can be emitted, but a
// Sixel-based sprixel must then be printed afresh. a Kitty-based sprixel
// needn't be touched in this case.
//
// a glyph above a TRANSPARENT sprixcell requires annihilating the underlying
// cell, but this is a special annihilation which never requires a wipe nor
// redisplay, just the O(1) state transition. a glyph below a TRANSPARENT
// sprixcell can be emitted with no change to the sprixcell. TRANSPARENT
// sprixcells move to ANNIHILATED_TRANS upon annihilation.
//
// a glyph above an ANNIHILATED sprixcell can be emitted with no change to
// the sprixcell. it does not make sense to emit a glyph below an ANNIHILATED
// sprixcell; if there is no longer a glyph above the sprixcell, the sprixcell
// must transition back to its original state (see below).
//
// rendering a new RGBA frame into the same sprixel plane can result in changes
// between OPAQUE, MIXED, and TRANSPARENT. an OPAQUE sprixcell which becomes
// TRANSPARENT or MIXED upon rendering a new RGBA frame must damage its cell,
// since the glyph underneath might have changed without being emitted. the
// new glyph must be emitted prior to redisplay of the sprixel.
//
// an ANNIHILATED sprixcell with no glyph above it must be restored to its
// original form (from the most recent RGBA frame). this requires the original
// pixel data. for Sixel, we must keep the palette indices in an auxiliary
// vector, hung off the TAM, updated each time we convert an RGBA frame into a
// partially- or wholly-ANNIHILATED sprixel. for Kitty, we must keep the
// original alpha values. the new state can be solved from this data. if the
// new state is either OPAQUE or MIXED, the sprixel must be redisplayed. if the
// new state is TRANSPARENT, this cell requires no such redisplay, and the
// payload needn't be modified. to special-case this O(1) conversion, we keep a
// distinct state, ANNIHILATED_TRANS. only a TRANSPARENT sprixcell can enter
// into this state.
//
// when a sprixel is removed from the rendering pile, in Sixel all cells it
// covered must be marked damaged, so that they are rendered, obliterating
// the bitmap. in Kitty the bitmap can simply be deleted, except for those
// cells which were SPRIXCELL_OPAQUE (they must be damaged).
//
// when a sprixel is moved, its TAM must be updated. OPAQUE, MIXED, and
// TRANSPARENT cells retain their entries. ANNIHILATED cells remain
// ANNIHILATED if their new absolute position corresponds to an ANNIHILATED
// cell; they otherwise transition back as outlined above. this is because
// ANNIHILATION is a property of those glyphs above us, while the other
// three are internal, intrinsic properties. for Sixel, all cells no longer
// covered must be damaged for rerendering, and the sprixel must subsequently
// be displayed at its new position. for Kitty, the sprixel must be deleted,
// and all cells no longer covered but which were previously under an OPAQUE
// cell must be damaged for rerendering (not to erase the bitmap, but because
// they might have changed without being emitted while obstructed by the
// sprixel). the sprixel should be displayed at its new position. using Kitty's
// bitmap movement is also acceptable, rather than a deletion and rerender.
// whichever method is used, it is necessary to recover any ANNIHILATED cells
// before moving or redisplaying the sprixel.
//
// all emissions take place at rasterization time. cell wiping happens at
// rendering time. cell reconstruction happens at rendering time (for
// ANNIHILATED cells which are no longer ANNIHILATED), or at blittime for
// a new RGBA frame.
typedef enum {
  SPRIXCELL_TRANSPARENT,       // all pixels are naturally transparent
  SPRIXCELL_OPAQUE_SIXEL,      // no transparent pixels in this cell
  SPRIXCELL_OPAQUE_KITTY,
  SPRIXCELL_MIXED_SIXEL,       // this cell has both opaque and transparent pixels
  SPRIXCELL_MIXED_KITTY,
  SPRIXCELL_ANNIHILATED,       // this cell has been wiped (all trans)
  SPRIXCELL_ANNIHILATED_TRANS, // this transparent cell is covered
} sprixcell_e;

// a TAM entry is a sprixcell_e state plus a possible auxiliary vector for
// reconstruction of annihilated cells, valid only for SPRIXCELL_ANNIHILATED.
typedef struct tament {
  sprixcell_e state;
  void* auxvector; // palette entries for sixel, alphas for kitty
} tament;

// a sprixel represents a bitmap, using whatever local protocol is available.
// there is a list of sprixels per ncpile. there ought never be very many
// associated with a context (a dozen or so at max). with the kitty protocol,
// we can register them, and then manipulate them by id. with the sixel
// protocol, we just have to rewrite them. there's a doubly-linked list of
// sprixels per ncpile, to which the pile keeps a head link.
typedef struct sprixel {
  fbuf glyph;
  uint32_t id;          // embedded into gcluster field of nccell, 24 bits
  // both the plane and visual can die before the sprixel does. they are
  // responsible in such a case for NULLing out this link themselves.
  struct ncplane* n;    // associated ncplane
  sprixel_e invalidated;// sprixel invalidation state
  struct sprixel* next;
  struct sprixel* prev;
  unsigned dimy, dimx;  // cell geometry
  int pixy, pixx;       // pixel geometry (might be smaller than cell geo)
  // each tacache entry is one of 0 (standard opaque cell), 1 (cell with
  // some transparency), 2 (annihilated, excised)
  int movedfromy;       // for SPRIXEL_MOVED, the starting absolute position,
  int movedfromx;       // so that we can damage old cells when redrawn
  // only used for kitty-based sprixels
  int parse_start;      // where to start parsing for cell wipes
  int pxoffy, pxoffx;   // X and Y parameters to display command
  // only used for sixel-based sprixels
  unsigned char* needs_refresh; // one per cell, whether new frame needs damage
  struct sixelmap* smap;  // copy of palette indices + transparency bits
  bool wipes_outstanding; // do we need rebuild the sixel next render?
  bool animating;        // do we have an active animation?
} sprixel;

static inline tament*
create_tam(int rows, int cols){
  // need cast for c++ callers
  tament* tam = (tament*)malloc(sizeof(*tam) * rows * cols);
  if(tam){
    memset(tam, 0, sizeof(*tam) * rows * cols);
  }
  return tam;
}

int sprite_init(struct tinfo* t, int fd);
int sixel_wipe(sprixel* s, int ycell, int xcell);
// nulls out a cell from a kitty bitmap via changing the alpha value
// throughout to 0. the same trick doesn't work on sixel, but there we
// can just print directly over the bitmap.
int kitty_wipe(sprixel* s, int ycell, int xcell);
// wipes out a cell by animating an all-transparent cell, and integrating
// it with the original image using the animation protocol of 0.20.0+.
int kitty_wipe_animation(sprixel* s, int ycell, int xcell);
int kitty_wipe_selfref(sprixel* s, int ycell, int xcell);
// wipes out a cell by changing the alpha value throughout the PNG cell to 0.
int fbcon_wipe(sprixel* s, int ycell, int xcell);
int sixel_rebuild(sprixel* s, int ycell, int xcell, uint8_t* auxvec);
int kitty_rebuild(sprixel* s, int ycell, int xcell, uint8_t* auxvec);
int fbcon_rebuild(sprixel* s, int ycell, int xcell, uint8_t* auxvec);
int kitty_rebuild_animation(sprixel* s, int ycell, int xcell, uint8_t* auxvec);
int kitty_rebuild_selfref(sprixel* s, int ycell, int xcell, uint8_t* auxvec);
int sixel_draw(const struct tinfo* ti, const struct ncpile *p, sprixel* s,
               fbuf* f, int yoff, int xoff);
int kitty_draw(const struct tinfo* ti, const struct ncpile *p, sprixel* s,
               fbuf* f, int yoff, int xoff);
int kitty_move(sprixel* s, fbuf* f, unsigned noscroll, int yoff, int xoff);
int sixel_scrub(const struct ncpile* p, sprixel* s);
int kitty_scrub(const struct ncpile* p, sprixel* s);
int fbcon_scrub(const struct ncpile* p, sprixel* s);
int kitty_remove(int id, fbuf* f);
int kitty_clear_all(fbuf* f);
int sixel_init_forcesdm(struct tinfo* ti, int fd);
int sixel_init_inverted(struct tinfo* ti, int fd);
int sixel_init(struct tinfo* ti, int fd);
uint8_t* sixel_trans_auxvec(const struct ncpile* p);
uint8_t* kitty_trans_auxvec(const struct ncpile* p);
int kitty_commit(fbuf* f, sprixel* s, unsigned noscroll);
int sixel_blit(struct ncplane* nc, int linesize, const void* data,
               int leny, int lenx, const struct blitterargs* bargs);
int kitty_blit(struct ncplane* nc, int linesize, const void* data,
               int leny, int lenx, const struct blitterargs* bargs);
int kitty_blit_animated(struct ncplane* n, int linesize, const void* data,
                        int leny, int lenx, const struct blitterargs* bargs);
int kitty_blit_selfref(struct ncplane* nc, int linesize, const void* data,
                       int leny, int lenx, const struct blitterargs* bargs);
int fbcon_blit(struct ncplane* nc, int linesize, const void* data,
               int leny, int lenx, const struct blitterargs* bargs);
int fbcon_draw(const struct tinfo* ti, sprixel* s, int yoff, int xoff);
void fbcon_scroll(const struct ncpile* p, struct tinfo* ti, int rows);
void sixel_refresh(const struct ncpile* p, sprixel* s);

// takes ownership of s on success.
int sprixel_load(sprixel* spx, fbuf* f, unsigned pixy, unsigned pixx,
                 int parse_start, sprixel_e state);

// called when a sprixel's cell-pixel geometry needs to change to |ncellpxy,ncellpxx|.
int sprixel_rescale(sprixel* spx, unsigned ncellpixy, unsigned ncellpixx);

// cleans up the sixel worker threads
void sixel_cleanup(struct tinfo* ti);

#ifdef __cplusplus
}
#endif

#endif
