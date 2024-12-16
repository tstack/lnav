#include <math.h>
#include <string.h>
#include "builddef.h"
#include "visual-details.h"
#include "internal.h"
#include "sixel.h"

// ncvisual core code has a basic implementation in libnotcurses-core, and can
// be augmented with a "multimedia engine" -- currently FFmpeg or OpenImageIO,
// or the trivial "none" engine. all libnotcurses (built against one of these
// engines, selected at compile time) actually does is set this
// visual_implementation pointer, and then call libnotcurses_core_init(). the
// "none" implementation exists to facilitate linking programs written against
// libnotcurses in environments without a true multimedia engine, and does not
// set this pointer. all this machination exists to support building notcurses
// (and running notcurses programs) without the need of heavy media engines.

static ncvisual_implementation null_visual_implementation = {0};

ncvisual_implementation* visual_implementation = &null_visual_implementation;

// to be called at startup -- performs any necessary engine initialization.
int ncvisual_init(int logl){
  if(visual_implementation->visual_init){
    return visual_implementation->visual_init(logl);
  }
  return 0;
}

void ncvisual_printbanner(fbuf* f){
  if(visual_implementation->visual_printbanner){
    visual_implementation->visual_printbanner(f);
  }
}

// you need an actual multimedia implementation for functions which work with
// codecs, including ncvisual_decode(), ncvisual_decode_loop(),
// ncvisual_from_file(), ncvisual_stream(), and ncvisual_subtitle_plane().
int ncvisual_decode(ncvisual* nc){
  if(!visual_implementation->visual_decode){
    return -1;
  }
  return visual_implementation->visual_decode(nc);
}

int ncvisual_decode_loop(ncvisual* nc){
  if(!visual_implementation->visual_decode_loop){
    return -1;
  }
  return visual_implementation->visual_decode_loop(nc);
}

ncvisual* ncvisual_from_file(const char* filename){
  if(!visual_implementation->visual_from_file){
    return NULL;
  }
  ncvisual* n = visual_implementation->visual_from_file(filename);
  if(n == NULL){
    logerror("error loading %s", filename);
  }
  return n;
}

int ncvisual_stream(notcurses* nc, ncvisual* ncv, float timescale,
                    ncstreamcb streamer, const struct ncvisual_options* vopts,
                    void* curry){
  if(!visual_implementation->visual_stream){
    return -1;
  }
  int ret = visual_implementation->visual_stream(nc, ncv, timescale, streamer, vopts, curry);
  if(ret < 0){
    logerror("error streaming media");
  }
  return ret;
}

ncplane* ncvisual_subtitle_plane(ncplane* parent, const ncvisual* ncv){
  if(!visual_implementation->visual_subtitle){
    return NULL;
  }
  return visual_implementation->visual_subtitle(parent, ncv);
}

int ncvisual_blit_internal(const ncvisual* ncv, int rows, int cols, ncplane* n,
                           const struct blitset* bset, const blitterargs* barg){
  if(!(barg->flags & NCVISUAL_OPTION_NOINTERPOLATE)){
    if(visual_implementation->visual_blit){
      if(visual_implementation->visual_blit(ncv, rows, cols, n, bset, barg) < 0){
        return -1;
      }
      return 0;
    }
  }
  // generic implementation
  int stride = 4 * cols;
  uint32_t* data = resize_bitmap(ncv->data, ncv->pixy, ncv->pixx,
                                 ncv->rowstride, rows, cols, stride);
  if(data == NULL){
    return -1;
  }
  int ret = -1;
  if(rgba_blit_dispatch(n, bset, stride, data, rows, cols, barg) >= 0){
    ret = 0;
  }
  if(data != ncv->data){
    free(data);
  }
  return ret;
}

// ncv constructors other than ncvisual_from_file() need to set up the
// AVFrame* 'frame' according to their own data, which is assumed to
// have been prepared already in 'ncv'.
void ncvisual_details_seed(struct ncvisual* ncv){
  if(visual_implementation->visual_details_seed){
    visual_implementation->visual_details_seed(ncv);
  }
}

ncvisual* ncvisual_create(void){
  if(visual_implementation->visual_create){
    return visual_implementation->visual_create();
  }
  ncvisual* ret = malloc(sizeof(*ret));
  if(ret){
    memset(ret, 0, sizeof(*ret));
  }
  return ret;
}

static inline void
ncvisual_origin(const struct ncvisual_options* vopts, unsigned* restrict begy,
                unsigned* restrict begx){
  *begy = vopts ? vopts->begy : 0;
  *begx = vopts ? vopts->begx : 0;
}

// create a plane in which to blit the sprixel. |disppixx| and |disppixy| are
// scaled pixel geometry on output, and unused on input. |placey| and |placex|
// are used to position the new plane, and reset to 0 on output. |outy| and
// |outx| are true output geometry on output, and unused on input (actual input
// pixel geometry come from ncv->pixy and ncv->pixx).
// |pxoffy| and |pxoffx| are pixel offset within the origin cell. they are not
// included within |disppixx| nor |disppixy|, but count towards |outx| and
// |outy|. these last two are furthermore clamped to sixel maxima, and |outy|
// accounts for sixels being a multiple of six pixels tall.
//
// cellpxy/cellpxx and dimy/dimx ought describe the cell-pixel and cell
// geometry of the target pile or, in Direct Mode, the tcache.
static void
shape_sprixel_plane(const tinfo* ti, unsigned cellpxy, unsigned cellpxx,
                    unsigned dimy, unsigned dimx,
                    ncplane* parent, const ncvisual* ncv,
                    ncscale_e scaling, unsigned* disppixy, unsigned* disppixx,
                    uint64_t flags, unsigned* outy, unsigned* outx,
                    int* placey, int* placex, int pxoffy, int pxoffx){
  if(scaling != NCSCALE_NONE && scaling != NCSCALE_NONE_HIRES){
    // disppixy/disppix are treated initially as cells
    if(parent == NULL){
      *disppixy = dimy;
      *disppixx = dimx;
    }else{
      ncplane_dim_yx(parent, disppixy, disppixx);
    }
    // FIXME why do we clamp only vertical, not horizontal, here?
    if(*placey + *disppixy >= dimy){
      *disppixy = dimy - *placey;
    }
    if(!(flags & NCVISUAL_OPTION_VERALIGNED)){
      *disppixy -= *placey;
    }
    if(!(flags & NCVISUAL_OPTION_HORALIGNED)){
      *disppixx -= *placex;
    }
    *disppixx *= cellpxx;
    *disppixy *= cellpxy;
    *disppixx += pxoffx;
    *disppixy += pxoffy;
    *outx = *disppixx;
    clamp_to_sixelmax(ti, disppixy, disppixx, outy, scaling);
    if(scaling == NCSCALE_SCALE || scaling == NCSCALE_SCALE_HIRES){
      scale_visual(ncv, disppixy, disppixx); // can only shrink
      *outx = *disppixx;
      clamp_to_sixelmax(ti, disppixy, disppixx, outy, scaling);
    }
  }else{
    *disppixx = ncv->pixx + pxoffx;
    *disppixy = ncv->pixy + pxoffy;
    *outx = *disppixx;
    clamp_to_sixelmax(ti, disppixy, disppixx, outy, scaling);
  }
  // pixel offsets ought be counted for clamping purposes, but not returned
  // as part of the scaled geometry (they are included in outx/outy).
  *disppixy -= pxoffy;
  *disppixx -= pxoffx;
}

// in addition to the fields in 'geom', we pass out:
//  * 'disppixx'/'disppixy': scaled output size in pixels
//  * 'outy'/'outx': true output size in pixels (ie post-sixel clamping)
//  * 'placey'/'placex': offset at which to draw
//  * 'bset': blitter that will be used
// we take in:
//  * 'p': target pile (for cell-pixel and cell geometry)
//  * 'ti': used if p is NULL (direct mode only!)
//  * 'n': input ncvisual
//  * 'vopts': requested ncvisual_options
int ncvisual_geom_inner(const tinfo* ti, const ncvisual* n,
                        const struct ncvisual_options* vopts, ncvgeom* geom,
                        const struct blitset** bset,
                        unsigned* disppixy, unsigned* disppixx,
                        unsigned* outy, unsigned* outx,
                        int* placey, int* placex){
  if(ti == NULL && n == NULL){
    logerror("got NULL for both sources");
    return -1;
  }
  struct ncvisual_options fakevopts;
  if(vopts == NULL){
    memset(&fakevopts, 0, sizeof(fakevopts));
    vopts = &fakevopts;
  }
  // check basic vopts preconditions
  if(vopts->flags >= (NCVISUAL_OPTION_NOINTERPOLATE << 1u)){
    logwarn("warning: unknown ncvisual options %016" PRIx64, vopts->flags);
  }
  if((vopts->flags & NCVISUAL_OPTION_CHILDPLANE) && !vopts->n){
    logerror("requested child plane with NULL n");
    return -1;
  }
  if(vopts->flags & NCVISUAL_OPTION_HORALIGNED){
    if(vopts->x < NCALIGN_UNALIGNED || vopts->x > NCALIGN_RIGHT){
      logerror("bad x %d for horizontal alignment", vopts->x);
      return -1;
    }
  }
  if(vopts->flags & NCVISUAL_OPTION_VERALIGNED){
    if(vopts->y < NCALIGN_UNALIGNED || vopts->y > NCALIGN_RIGHT){
      logerror("bad y %d for vertical alignment", vopts->y);
      return -1;
    }
  }
  if(n){
    geom->pixy = n->pixy;
    geom->pixx = n->pixx;
  }
  // when ti is NULL, we only report properties intrinsic to the ncvisual,
  // i.e. only its original pixel geometry.
  if(ti == NULL){
    return 0;
  }
  // determine our blitter
  *bset = rgba_blitter(ti, vopts);
  if(!*bset){
    logerror("couldn't get a blitter for %d", vopts ? vopts->blitter : NCBLIT_DEFAULT);
    return -1;
  }
  const ncpile* p = vopts->n ? ncplane_pile_const(vopts->n) : NULL;
  geom->cdimy = p ? p->cellpxy : ti->cellpxy;
  geom->cdimx = p ? p->cellpxx : ti->cellpxx;
  if((geom->blitter = (*bset)->geom) == NCBLIT_PIXEL){
    geom->maxpixely = ti->sixel_maxy;
    geom->maxpixelx = ti->sixel_maxx;
  }
  geom->scaley = encoding_y_scale(ti, *bset);
  geom->scalex = encoding_x_scale(ti, *bset);
  // when n is NULL, we only report properties unrelated to the ncvisual,
  // i.e. the cell-pixel geometry, max bitmap geometry, blitter, and scaling.
  if(n == NULL){
    return 0;
  }
  ncscale_e scaling = vopts ? vopts->scaling : NCSCALE_NONE;
  // determine how much of the original image we're using (leny/lenx)
  ncvisual_origin(vopts, &geom->begy, &geom->begx);
  geom->lenx = vopts->lenx;
  geom->leny = vopts->leny;
  *placey = vopts->y;
  *placex = vopts->x;
  logdebug("vis %ux%u+%ux%u %p", geom->begy, geom->begx, geom->leny, geom->lenx, n->data);
  if(n->data == NULL){
    logerror("no data in visual");
    return -1;
  }
  if(geom->begx >= n->pixx || geom->begy >= n->pixy){
    logerror("visual too large %u > %d or %u > %d", geom->begy, n->pixy, geom->begx, n->pixx);
    return -1;
  }
  if(geom->lenx == 0){ // 0 means "to the end"; use all available source material
    geom->lenx = n->pixx - geom->begx;
  }
  if(geom->leny == 0){
    geom->leny = n->pixy - geom->begy;
  }
  if(geom->lenx <= 0 || geom->leny <= 0){ // no need to draw zero-size object, exit
    logerror("zero-size object %d %d", geom->leny, geom->lenx);
    return -1;
  }
  if(geom->begx + geom->lenx > n->pixx || geom->begy + geom->leny > n->pixy){
    logerror("geometry too large %d > %d or %d > %d", geom->begy + geom->leny, n->pixy, geom->begx + geom->lenx, n->pixx);
    return -1;
  }
  if((*bset)->geom == NCBLIT_PIXEL){
    if(vopts->n){
      // FIXME does this work from direct mode?
      if(vopts->n == notcurses_stdplane_const(ncplane_notcurses_const(vopts->n))){
        if(!(vopts->flags & NCVISUAL_OPTION_CHILDPLANE)){
          logerror("won't blit bitmaps to the standard plane");
          return -1;
        }
      }
      if(vopts->y && !(vopts->flags & (NCVISUAL_OPTION_VERALIGNED | NCVISUAL_OPTION_CHILDPLANE))){
        logerror("non-origin y placement %d for sprixel", vopts->y);
        return -1;
      }
      if(vopts->x && !(vopts->flags & (NCVISUAL_OPTION_HORALIGNED | NCVISUAL_OPTION_CHILDPLANE))){
        logerror("non-origin x placement %d for sprixel", vopts->x);
        return -1;
      }
      if(vopts->pxoffy >= geom->cdimy){
        logerror("pixel y-offset %d too tall for cell %d", vopts->pxoffy, geom->cdimy);
        return -1;
      }
      if(vopts->pxoffx >= geom->cdimx){
        logerror("pixel x-offset %d too wide for cell %d", vopts->pxoffx, geom->cdimx);
        return -1;
      }
      if(scaling == NCSCALE_NONE || scaling == NCSCALE_NONE_HIRES){
        // FIXME clamp to sprixel limits
        unsigned rows = ((geom->leny + geom->cdimy - 1) / geom->cdimy) + !!vopts->pxoffy;
        if(rows > ncplane_dim_y(vopts->n)){
          logerror("sprixel too tall %d for plane %d", geom->leny + vopts->pxoffy,
                   ncplane_dim_y(vopts->n) * geom->cdimy);
          return -1;
        }
        unsigned cols = ((geom->lenx + geom->cdimx - 1) / geom->cdimx) + !!vopts->pxoffx;
        if(cols > ncplane_dim_x(vopts->n)){
          logerror("sprixel too wide %d for plane %d", geom->lenx + vopts->pxoffx,
                   ncplane_dim_x(vopts->n) * geom->cdimx);
          return -1;
        }
      }
    }
    if(vopts->n == NULL || (vopts->flags & NCVISUAL_OPTION_CHILDPLANE)){
      // we'll need to create the plane
      const int dimy = p ? p->dimy : ti->dimy;
      const int dimx = p ? p->dimx : ti->dimx;
      shape_sprixel_plane(ti, geom->cdimy, geom->cdimx, dimy, dimx,
                          vopts->n, n, scaling, disppixy, disppixx,
                          vopts->flags, outy, outx, placey, placex,
                          vopts->pxoffy, vopts->pxoffx);
    }else{
      if(scaling != NCSCALE_NONE && scaling != NCSCALE_NONE_HIRES){
        ncplane_dim_yx(vopts->n, disppixy, disppixx);
        *disppixx *= geom->cdimx;
        *disppixx += vopts->pxoffx;
        *disppixy *= geom->cdimy;
        *disppixy += vopts->pxoffy;
        clamp_to_sixelmax(ti, disppixy, disppixx, outy, scaling);
        int absplacex = 0, absplacey = 0;
        if(!(vopts->flags & NCVISUAL_OPTION_HORALIGNED)){
          absplacex = *placex;
        }
        if(!(vopts->flags & NCVISUAL_OPTION_VERALIGNED)){
          absplacey = *placey;
        }
        *disppixx -= absplacex * geom->cdimx;
        *disppixy -= absplacey * geom->cdimy;
      }else{
        *disppixx = geom->lenx + vopts->pxoffx;
        *disppixy = geom->leny + vopts->pxoffy;
      }
      logdebug("pixel prescale: %d %d %d %d", n->pixy, n->pixx, *disppixy, *disppixx);
      if(scaling == NCSCALE_SCALE || scaling == NCSCALE_SCALE_HIRES){
        clamp_to_sixelmax(ti, disppixy, disppixx, outy, scaling);
        scale_visual(n, disppixy, disppixx);
      }
      clamp_to_sixelmax(ti, disppixy, disppixx, outy, scaling);
      // FIXME use a closed form
      while((*outy + geom->cdimy - 1) / geom->cdimy > ncplane_dim_y(vopts->n)){
        *outy -= ti->sprixel_scale_height;
        *disppixy = *outy;
      }
      *outx = *disppixx;
      *disppixx -= vopts->pxoffx;
      *disppixy -= vopts->pxoffy;
    }
    logdebug("pblit: %dx%d ← %dx%d of %d/%d stride %u @%dx%d %p %u", *disppixy, *disppixx, geom->begy, geom->begx, n->pixy, n->pixx, n->rowstride, *placey, *placex, n->data, geom->cdimx);
    geom->rpixy = *disppixy;
    geom->rpixx = *disppixx;
    geom->rcellx = *outx / geom->cdimx + !!(*outx % geom->cdimx);
    geom->rcelly = *outy / geom->cdimy + !!(*outy % geom->cdimy);
  }else{ // cellblit
    if(vopts->pxoffx || vopts->pxoffy){
      logerror("pixel offsets cannot be used with cell blitting");
      return -1;
    }
    unsigned dispcols, disprows;
    if(vopts->n == NULL || (vopts->flags & NCVISUAL_OPTION_CHILDPLANE)){ // create plane
//fprintf(stderr, "CPATH1, create beg %dx%d len %dx%d\n", geom->begy, geom->begx, geom->leny, geom->lenx);
      if(scaling == NCSCALE_NONE || scaling == NCSCALE_NONE_HIRES){
        dispcols = geom->lenx;
        disprows = geom->leny;
      }else{
        if(vopts->n == NULL){
          disprows = ti->dimy;
          dispcols = ti->dimx;
        }else{
          ncplane_dim_yx(vopts->n, &disprows, &dispcols);
        }
        dispcols *= geom->scalex;
        disprows *= geom->scaley;
        if(scaling == NCSCALE_SCALE || scaling == NCSCALE_SCALE_HIRES){
          scale_visual(n, &disprows, &dispcols);
        } // else stretch
      }
    }else{
//fprintf(stderr, "CPATH2, reuse beg %dx%d len %dx%d\n", geom->begy, geom->begx, geom->leny, geom->lenx);
      if(scaling == NCSCALE_NONE || scaling == NCSCALE_NONE_HIRES){
        dispcols = geom->lenx;
        disprows = geom->leny;
      }else{
        ncplane_dim_yx(vopts->n, &disprows, &dispcols);
        dispcols *= geom->scalex;
        disprows *= geom->scaley;
        if(!(vopts->flags & NCVISUAL_OPTION_HORALIGNED)){
          dispcols -= *placex;
        }
        if(!(vopts->flags & NCVISUAL_OPTION_VERALIGNED)){
          disprows -= *placey;
        }
        if(scaling == NCSCALE_SCALE || scaling == NCSCALE_SCALE_HIRES){
          scale_visual(n, &disprows, &dispcols);
        } // else stretch
      }
      if(vopts->flags & NCVISUAL_OPTION_HORALIGNED){
        *placex = ncplane_halign(vopts->n, *placex, dispcols / geom->scalex);
      }
      if(vopts->flags & NCVISUAL_OPTION_VERALIGNED){
        *placey = ncplane_valign(vopts->n, *placey, disprows / geom->scaley);
      }
    }
    geom->rpixy = disprows;
    geom->rpixx = dispcols;
    geom->rcellx = dispcols / geom->scalex + !!(dispcols % geom->scalex);
    geom->rcelly = disprows / geom->scaley + !!(disprows % geom->scaley);
  }
  logdebug("rgeom: %d %d %d %d @ %d/%d (%d on %p)", geom->rcelly, geom->rcellx,
           geom->rpixy, geom->rpixx, *placey, *placex, (*bset)->geom, vopts->n);
  return 0;
}

int ncvisual_geom(const notcurses* nc, const ncvisual* n,
                  const struct ncvisual_options* vopts, ncvgeom* geom){
  const struct blitset* bset;
  unsigned disppxy, disppxx, outy, outx;
  int placey, placex;
  return ncvisual_geom_inner(nc ? &nc->tcache : NULL, n, vopts, geom, &bset,
                             &disppxy, &disppxx, &outy, &outx, &placey, &placex);
}

void* rgb_loose_to_rgba(const void* data, int rows, int* rowstride, int cols, int alpha){
  if(*rowstride % 4){ // must be a multiple of 4 bytes
    return NULL;
  }
  if(*rowstride < cols * 4){
    return NULL;
  }
  uint32_t* ret = malloc(4 * cols * rows);
  if(ret){
    for(int y = 0 ; y < rows ; ++y){
      for(int x = 0 ; x < cols ; ++x){
        const uint32_t* src = (const uint32_t*)data + (*rowstride / 4) * y + x;
        uint32_t* dst = ret + cols * y + x;
        ncpixel_set_a(dst, alpha);
        ncpixel_set_r(dst, ncpixel_r(*src));
        ncpixel_set_g(dst, ncpixel_g(*src));
        ncpixel_set_b(dst, ncpixel_b(*src));
      }
    }
  }
  *rowstride = cols * 4;
  return ret;
}

void* rgb_packed_to_rgba(const void* data, int rows, int* rowstride, int cols, int alpha){
  if(*rowstride < cols * 3){
    return NULL;
  }
  uint32_t* ret = malloc(4 * cols * rows);
  if(ret){
    for(int y = 0 ; y < rows ; ++y){
      for(int x = 0 ; x < cols ; ++x){
        const unsigned char* src = (const unsigned char*)data + *rowstride * y + x;
        uint32_t* dst = ret + cols * y + x;
        ncpixel_set_a(dst, alpha);
        ncpixel_set_r(dst, src[0]);
        ncpixel_set_g(dst, src[1]);
        ncpixel_set_b(dst, src[2]);
      }
    }
  }
  *rowstride = cols * 4;
  return ret;
}

void* bgra_to_rgba(const void* data, int rows, int* rowstride, int cols, int alpha){
  if(*rowstride % 4){ // must be a multiple of 4 bytes
    return NULL;
  }
  if(*rowstride < cols * 4){
    return NULL;
  }
  uint32_t* ret = malloc(4 * cols * rows);
  if(ret){
    for(int y = 0 ; y < rows ; ++y){
      for(int x = 0 ; x < cols ; ++x){
        const uint32_t* src = (const uint32_t*)data + (*rowstride / 4) * y + x;
        uint32_t* dst = ret + cols * y + x;
        ncpixel_set_a(dst, alpha);
        ncpixel_set_r(dst, ncpixel_b(*src));
        ncpixel_set_g(dst, ncpixel_g(*src));
        ncpixel_set_b(dst, ncpixel_r(*src));
      }
    }
  }
  *rowstride = cols * 4;
  return ret;
}

// Inspects the visual to find the minimum rectangle that can contain all
// "real" pixels, where "real" pixels are, by convention, all zeroes.
// Placing this box at offyXoffx relative to the visual will encompass all
// pixels. Returns the area of the box (0 if there are no pixels).
int ncvisual_bounding_box(const ncvisual* ncv, int* leny, int* lenx,
                          int* offy, int* offx){
  unsigned lcol = 0;
  unsigned rcol = UINT_MAX;
  unsigned trow;
  // first, find the topmost row with a real pixel. if there is no such row,
  // there are no such pixels. if we find one, we needn't look in this region
  // for other extrema, so long as we keep the leftmost and rightmost through
  // this row (from the top). said leftmost and rightmost will be the leftmost
  // and rightmost pixel of whichever row has the topmost valid pixel. unlike
  // the topmost, they'll need be further verified.
  for(trow = 0 ; trow < ncv->pixy ; ++trow){
    for(unsigned x = 0 ; x < ncv->pixx ; ++x){
      uint32_t rgba = ncv->data[trow * ncv->rowstride / 4 + x];
      if(rgba){
        lcol = x; // leftmost pixel of topmost row
        // now find rightmost pixel of topmost row
        unsigned xr;
        for(xr = ncv->pixx - 1 ; xr > x ; --xr){
          rgba = ncv->data[trow * ncv->rowstride / 4 + xr];
          if(rgba){ // rightmost pixel of topmost row
            break;
          }
        }
        rcol = xr;
        break;
      }
    }
    if(rcol < INT_MAX){
      break;
    }
  }
  if(trow == ncv->pixy){ // no real pixels
    *leny = 0;
    *lenx = 0;
    *offy = 0;
    *offx = 0;
  }else{
    assert(rcol < ncv->pixx);
    // we now know topmost row, and left/rightmost through said row. now we must
    // find the bottommost row, checking left/rightmost throughout.
    unsigned brow;
    for(brow = ncv->pixy - 1 ; brow > trow ; --brow){
      unsigned x;
      for(x = 0 ; x < ncv->pixx ; ++x){
        uint32_t rgba = ncv->data[brow * ncv->rowstride / 4 + x];
        if(rgba){
          if(x < lcol){
            lcol = x;
          }
          unsigned xr;
          for(xr = ncv->pixx - 1 ; xr > x && xr > rcol ; --xr){
            rgba = ncv->data[brow * ncv->rowstride / 4 + xr];
            if(rgba){ // rightmost pixel of bottommost row
              if(xr > rcol){
                rcol = xr;
              }
              break;
            }
          }
          break;
        }
      }
      if(x < ncv->pixx){
        break;
      }
    }
    // we now know topmost and bottommost row, and left/rightmost within those
    // two sections. now check the rest for left and rightmost.
    for(unsigned y = trow + 1 ; y < brow ; ++y){
      for(unsigned x = 0 ; x < lcol ; ++x){
        uint32_t rgba = ncv->data[y * ncv->rowstride / 4 + x];
        if(rgba){
          lcol = x;
          break;
        }
      }
      for(unsigned x = ncv->pixx - 1 ; x > rcol ; --x){
        uint32_t rgba = ncv->data[y * ncv->rowstride / 4 + x];
        if(rgba){
          rcol = x;
          break;
        }
      }
    }
    *offy = trow;
    *leny = brow - trow + 1;
    *offx = lcol;
    *lenx = rcol - lcol + 1;
  }
  return *leny * *lenx;
}

// find the "center" cell of a visual. in the case of even rows/columns, we
// place the center on the top/left. in such a case there will be one more
// cell to the bottom/right of the center.
static inline void
ncvisual_center(const ncvisual* n, int* RESTRICT y, int* RESTRICT x){
  *y = n->pixy;
  *x = n->pixx;
  center_box(y, x);
}

// rotate the 0-indexed (origin-indexed) ['y', 'x'] through 'ctheta' and
// 'stheta' around the centerpoint at ['centy', 'centx']. write the results
// back to 'y' and 'x'.
static void
rotate_point(int* y, int* x, double stheta, double ctheta, int centy, int centx){
  // convert coordinates from origin to left-handed cartesian
  const int convx = *x - centx;
  const int convy = *y - centy;
//fprintf(stderr, "%d, %d -> conv %d, %d\n", *y, *x, convy, convx);
  *x = round(convx * ctheta - convy * stheta);
  *y = round(convx * stheta + convy * ctheta);
}

// rotate the specified bounding box by the specified sine and cosine of some
// theta radians, enlarging or shrinking it as necessary. returns the area.
// 'leny', 'lenx', 'offy', and 'offx' describe the bounding box to be rotated,
// and might all be updated (in either direction).
static int
rotate_bounding_box(double stheta, double ctheta, int* leny, int* lenx,
                    int* offy, int* offx){
//fprintf(stderr, "Incoming bounding box: %dx%d @ %dx%d rotate s(%f) c(%f)\n", *leny, *lenx, *offy, *offx, stheta, ctheta);
  int xs[4], ys[4]; // x and y locations of rotated coordinates
  int centy = *leny;
  int centx = *lenx;
  center_box(&centy, &centx);
  ys[0] = 0;
  xs[0] = 0;
  rotate_point(ys, xs, stheta, ctheta, centy, centx);
//fprintf(stderr, "rotated %d, %d -> %d %d\n", 0, 0, ys[0], xs[0]);
  ys[1] = 0;
  xs[1] = *lenx - 1;
  rotate_point(ys + 1, xs + 1, stheta, ctheta, centy, centx);
//fprintf(stderr, "rotated %d, %d -> %d %d\n", 0, *lenx - 1, ys[1], xs[1]);
  ys[2] = *leny - 1;
  xs[2] = *lenx - 1;
  rotate_point(ys + 2, xs + 2, stheta, ctheta, centy, centx);
//fprintf(stderr, "rotated %d, %d -> %d %d\n", *leny - 1, *lenx - 1, ys[2], xs[2]);
  ys[3] = *leny - 1;
  xs[3] = 0;
  rotate_point(ys + 3, xs + 3, stheta, ctheta, centy, centx);
//fprintf(stderr, "rotated %d, %d -> %d %d\n", *leny - 1, 0, ys[3], xs[3]);
  int trow = ys[0];
  int brow = ys[0];
  int lcol = xs[0];
  int rcol = xs[0];
  for(size_t i = 1 ; i < sizeof(xs) / sizeof(*xs) ; ++i){
    if(xs[i] < lcol){
      lcol = xs[i];
    }
    if(xs[i] > rcol){
      rcol = xs[i];
    }
    if(ys[i] < trow){
      trow = ys[i];
    }
    if(ys[i] > brow){
      brow = ys[i];
    }
  }
  *offy = trow;
  *leny = brow - trow + 1;
  *offx = lcol;
  *lenx = rcol - lcol + 1;
//fprintf(stderr, "Rotated bounding box: %dx%d @ %dx%d\n", *leny, *lenx, *offy, *offx);
  return *leny * *lenx;
}

int ncvisual_rotate(ncvisual* ncv, double rads){
  assert(ncv->rowstride / 4 >= ncv->pixx);
  rads = -rads; // we're a left-handed Cartesian
  int centy, centx;
  ncvisual_center(ncv, &centy, &centx); // pixel center (center of 'data')
  double stheta, ctheta; // sine, cosine
  stheta = sin(rads);
  ctheta = cos(rads);
  // bounding box for real data within the ncvisual. we must only resize to
  // accommodate real data, lest we grow without band as we rotate.
  // see https://github.com/dankamongmen/notcurses/issues/599.
  int bby = ncv->pixy;
  int bbx = ncv->pixx;
  int bboffy = 0;
  int bboffx = 0;
  if(ncvisual_bounding_box(ncv, &bby, &bbx, &bboffy, &bboffx) <= 0){
    logerror("couldn't find a bounding box");
    return -1;
  }
  int bbarea;
  bbarea = rotate_bounding_box(stheta, ctheta, &bby, &bbx, &bboffy, &bboffx);
  if(bbarea <= 0){
    logerror("couldn't rotate the visual (%d, %d, %d, %d)", bby, bbx, bboffy, bboffx);
    return -1;
  }
  int bbcentx = bbx, bbcenty = bby;
  center_box(&bbcenty, &bbcentx);
//fprintf(stderr, "stride: %d height: %d width: %d\n", ncv->rowstride, ncv->pixy, ncv->pixx);
  assert(ncv->rowstride / 4 >= ncv->pixx);
  uint32_t* data = malloc(bbarea * 4);
  if(data == NULL){
    return -1;
  }
  memset(data, 0, bbarea * 4);
//fprintf(stderr, "bbarea: %d bby: %d bbx: %d centy: %d centx: %d bbcenty: %d bbcentx: %d\n", bbarea, bby, bbx, centy, centx, bbcenty, bbcentx);
  for(unsigned y = 0 ; y < ncv->pixy ; ++y){
      for(unsigned x = 0 ; x < ncv->pixx ; ++x){
      int targx = x, targy = y;
      rotate_point(&targy, &targx, stheta, ctheta, centy, centx);
      if(targx > bboffx && targy > bboffy){
        const int deconvx = targx - bboffx;
        const int deconvy = targy - bboffy;
        if(deconvy < bby && deconvx < bbx){
          data[deconvy * bbx + deconvx] = ncv->data[y * (ncv->rowstride / 4) + x];
        }
      }
//fprintf(stderr, "CW: %d/%d (%08x) -> %d/%d (stride: %d)\n", y, x, ncv->data[y * (ncv->rowstride / 4) + x], targy, targx, ncv->rowstride);
//fprintf(stderr, "wrote %08x to %d (%d)\n", data[targy * ncv->pixy + targx], targy * ncv->pixy + targx, (targy * ncv->pixy + targx) * 4);
    }
  }
  ncvisual_set_data(ncv, data, true);
  ncv->pixx = bbx;
  ncv->pixy = bby;
  ncv->rowstride = bbx * 4;
  ncvisual_details_seed(ncv);
  return 0;
}

static inline size_t
pad_for_image(size_t stride, int cols){
  if(visual_implementation->rowalign == 0){
    return 4 * cols;
  }else if(stride < cols * 4u){
    return (4 * cols + visual_implementation->rowalign) /
            visual_implementation->rowalign * visual_implementation->rowalign;
  }else if(stride % visual_implementation->rowalign == 0){
    return stride;
  }
  return (stride + visual_implementation->rowalign) /
          visual_implementation->rowalign * visual_implementation->rowalign;
}

ncvisual* ncvisual_from_rgba(const void* rgba, int rows, int rowstride, int cols){
  if(rowstride % 4){
    logerror("rowstride %d not a multiple of 4", rowstride);
    return NULL;
  }
  if(rowstride * 4 < cols || cols <= 0 || rows <= 0){
    logerror("invalid rowstride or geometry");
    return NULL;
  }
  ncvisual* ncv = ncvisual_create();
  if(ncv){
    // ffmpeg needs inputs with rows aligned on 192-byte boundaries
    ncv->rowstride = pad_for_image(rowstride, cols);
    ncv->pixx = cols;
    ncv->pixy = rows;
    uint32_t* data = malloc(ncv->rowstride * ncv->pixy);
    if(data == NULL){
      ncvisual_destroy(ncv);
      return NULL;
    }
    for(int y = 0 ; y < rows ; ++y){
//fprintf(stderr, "ROWS: %d STRIDE: %d (%d) COLS: %d %08x\n", ncv->pixy, ncv->rowstride, rowstride, cols, data[ncv->rowstride * y / 4]);
      memcpy(data + (ncv->rowstride * y) / 4, (const char*)rgba + rowstride * y, rowstride);
    }
    ncvisual_set_data(ncv, data, true);
    ncvisual_details_seed(ncv);
  }
  return ncv;
}

ncvisual* ncvisual_from_sixel(const char* s, unsigned leny, unsigned lenx){
  uint32_t* rgba = ncsixel_as_rgba(s, leny, lenx);
  if(rgba == NULL){
    logerror("failed converting sixel to rgba");
    return NULL;
  }
  ncvisual* ncv = ncvisual_from_rgba(rgba, leny, lenx * sizeof(*rgba), lenx);
  free(rgba);
  return ncv;
}

ncvisual* ncvisual_from_rgb_packed(const void* rgba, int rows, int rowstride,
                                   int cols, int alpha){
  if(rowstride % 3){
    logerror("rowstride %d not a multiple of 3", rowstride);
    return NULL;
  }
  if(rows <= 0 || cols <= 0 || rowstride < cols * 3){
    logerror("illegal packed rgb geometry");
    return NULL;
  }
  ncvisual* ncv = ncvisual_create();
  if(ncv){
    ncv->rowstride = pad_for_image(cols * 4, cols);
    ncv->pixx = cols;
    ncv->pixy = rows;
    uint32_t* data = malloc(ncv->rowstride * ncv->pixy);
    if(data == NULL){
      ncvisual_destroy(ncv);
      return NULL;
    }
    const unsigned char* src = rgba;
    for(int y = 0 ; y < rows ; ++y){
//fprintf(stderr, "ROWS: %d STRIDE: %d (%d) COLS: %d %08x\n", ncv->pixy, ncv->rowstride, ncv->rowstride / 4, cols, data[ncv->rowstride * y / 4]);
      for(int x = 0 ; x < cols ; ++x){
        unsigned char r, g, b;
        memcpy(&r, src + rowstride * y + 3 * x, 1);
        memcpy(&g, src + rowstride * y + 3 * x + 1, 1);
        memcpy(&b, src + rowstride * y + 3 * x + 2, 1);
        ncpixel_set_a(&data[y * ncv->rowstride / 4 + x], alpha);
        ncpixel_set_r(&data[y * ncv->rowstride / 4 + x], r);
        ncpixel_set_g(&data[y * ncv->rowstride / 4 + x], g);
        ncpixel_set_b(&data[y * ncv->rowstride / 4 + x], b);
//fprintf(stderr, "RGBA: 0x%02x 0x%02x 0x%02x 0x%02x\n", r, g, b, alpha);
      }
    }
    ncvisual_set_data(ncv, data, true);
    ncvisual_details_seed(ncv);
  }
  return ncv;
}

ncvisual* ncvisual_from_rgb_loose(const void* rgba, int rows, int rowstride,
                                  int cols, int alpha){
  if(rowstride % 4){
    logerror("rowstride %d not a multiple of 4", rowstride);
    return NULL;
  }
  if(rows <= 0 || cols <= 0 || rowstride < cols * 4){
    logerror("illegal packed rgb geometry");
    return NULL;
  }
  ncvisual* ncv = ncvisual_create();
  if(ncv){
    ncv->rowstride = pad_for_image(cols * 4, cols);
    ncv->pixx = cols;
    ncv->pixy = rows;
    uint32_t* data = malloc(ncv->rowstride * ncv->pixy);
    if(data == NULL){
      ncvisual_destroy(ncv);
      return NULL;
    }
    for(int y = 0 ; y < rows ; ++y){
//fprintf(stderr, "ROWS: %d STRIDE: %d (%d) COLS: %d %08x\n", ncv->pixy, ncv->rowstride, ncv->rowstride / 4, cols, data[ncv->rowstride * y / 4]);
      memcpy(data + (ncv->rowstride * y) / 4, (const char*)rgba + rowstride * y, rowstride);
      for(int x = 0 ; x < cols ; ++x){
        ncpixel_set_a(&data[y * ncv->rowstride / 4 + x], alpha);
      }
    }
    ncvisual_set_data(ncv, data, true);
    ncvisual_details_seed(ncv);
  }
  return ncv;
}

ncvisual* ncvisual_from_bgra(const void* bgra, int rows, int rowstride, int cols){
  if(rowstride % 4){
    logerror("rowstride %d not a multiple of 4", rowstride);
    return NULL;
  }
  if(rows <= 0 || cols <= 0 || rowstride < cols * 4){
    logerror("illegal bgra geometry");
    return NULL;
  }
  ncvisual* ncv = ncvisual_create();
  if(ncv){
    ncv->rowstride = pad_for_image(rowstride, cols);
    ncv->pixx = cols;
    ncv->pixy = rows;
    uint32_t* data = malloc(ncv->rowstride * ncv->pixy);
    if(data == NULL){
      ncvisual_destroy(ncv);
      return NULL;
    }
    for(int y = 0 ; y < rows ; ++y){
      for(int x = 0 ; x < cols ; ++x){
        uint32_t src;
        memcpy(&src, (const char*)bgra + y * rowstride + x * 4, 4);
        uint32_t* dst = &data[ncv->rowstride * y / 4 + x];
        ncpixel_set_a(dst, ncpixel_a(src));
        ncpixel_set_r(dst, ncpixel_b(src));
        ncpixel_set_g(dst, ncpixel_g(src));
        ncpixel_set_b(dst, ncpixel_r(src));
//fprintf(stderr, "BGRA PIXEL: %02x%02x%02x%02x RGBA result: %02x%02x%02x%02x\n", ((const char*)&src)[0], ((const char*)&src)[1], ((const char*)&src)[2], ((const char*)&src)[3], ((const char*)dst)[0], ((const char*)dst)[1], ((const char*)dst)[2], ((const char*)dst)[3]);
      }
    }
    ncvisual_set_data(ncv, data, true);
    ncvisual_details_seed(ncv);
  }
  return ncv;
}

ncvisual* ncvisual_from_palidx(const void* pdata, int rows, int rowstride,
                               int cols, int palsize, int pstride,
                               const uint32_t* palette){
  if(pstride <= 0 || rowstride % pstride){
    logerror("bad pstride (%d) for rowstride (%d)", pstride, rowstride);
    return NULL;
  }
  if(rows <= 0 || cols <= 0 || rowstride < cols * pstride){
    logerror("illegal palimg geometry");
    return NULL;
  }
  if(palsize > 256 || palsize <= 0){
    logerror("palettes size (%d) is unsupported", palsize);
    return NULL;
  }
  ncvisual* ncv = ncvisual_create();
  if(ncv){
    ncv->rowstride = pad_for_image(rowstride, cols);
    ncv->pixx = cols;
    ncv->pixy = rows;
    uint32_t* data = malloc(ncv->rowstride * ncv->pixy);
    if(data == NULL){
      ncvisual_destroy(ncv);
      return NULL;
    }
    for(int y = 0 ; y < rows ; ++y){
      for(int x = 0 ; x < cols ; ++x){
        int palidx = ((const unsigned char*)pdata)[y * rowstride + x * pstride];
        if(palidx >= palsize){
          free(data);
          ncvisual_destroy(ncv);
          logerror("invalid palette idx %d >= %d", palidx, palsize);
          return NULL;
        }
        uint32_t src = palette[palidx];
        uint32_t* dst = &data[ncv->rowstride * y / 4 + x];
        if(ncchannel_default_p(src)){
          // FIXME use default color as detected, or just 0xffffff
          ncpixel_set_a(dst, 255 - palidx);
          ncpixel_set_r(dst, palidx);
          ncpixel_set_g(dst, 220 - (palidx / 2));
          ncpixel_set_b(dst, palidx);
        }else{
          *dst = 0;
        }
//fprintf(stderr, "BGRA PIXEL: %02x%02x%02x%02x RGBA result: %02x%02x%02x%02x\n", ((const char*)&src)[0], ((const char*)&src)[1], ((const char*)&src)[2], ((const char*)&src)[3], ((const char*)dst)[0], ((const char*)dst)[1], ((const char*)dst)[2], ((const char*)dst)[3]);
      }
    }
    ncvisual_set_data(ncv, data, true);
    ncvisual_details_seed(ncv);
  }
  return ncv;
}

int ncvisual_resize(ncvisual* n, int rows, int cols){
  if(!visual_implementation->visual_resize){
    return ncvisual_resize_noninterpolative(n, rows, cols);
  }
  if(visual_implementation->visual_resize(n, rows, cols)){
    return -1;
  }
  return 0;
}

int ncvisual_resize_noninterpolative(ncvisual* n, int rows, int cols){
  size_t dstride = pad_for_image(cols * 4, cols);
  uint32_t* r = resize_bitmap(n->data, n->pixy, n->pixx, n->rowstride,
                              rows, cols, dstride);
  if(r == NULL){
    return -1;
  }
  ncvisual_set_data(n, r, true);
  n->rowstride = dstride;
  n->pixy = rows;
  n->pixx = cols;
  ncvisual_details_seed(n);
  return 0;
}

// by the end, disprows/dispcols refer to the number of source rows/cols (in
// pixels), which will be mapped to a region of cells scaled by the encodings).
// the blit will begin at placey/placex (in terms of cells). begy/begx define
// the origin of the source region to draw (in pixels). leny/lenx define the
// geometry of the source region to draw, again in pixels. ncv->pixy and
// ncv->pixx define the source geometry in pixels.
ncplane* ncvisual_render_cells(ncvisual* ncv, const struct blitset* bset,
                               int placey, int placex,
                               ncvgeom* geom, ncplane* n,
                               uint64_t flags, uint32_t transcolor){
  logdebug("cblit: rows/cols: %dx%d plane: %d/%d pix: %d/%d", geom->rcelly, geom->rcellx, ncplane_dim_y(n), ncplane_dim_x(n), geom->rpixy, geom->rpixx);
  blitterargs bargs;
  bargs.transcolor = transcolor;
  bargs.begy = geom->begy;
  bargs.begx = geom->begx;
  bargs.leny = geom->leny;
  bargs.lenx = geom->lenx;
  bargs.flags = flags;
  bargs.u.cell.placey = placey;
  bargs.u.cell.placex = placex;
  if(ncvisual_blit_internal(ncv, geom->rpixy, geom->rpixx, n, bset, &bargs)){
    return NULL;
  }
  return n;
}

// when a sprixel is blitted to a plane, that plane becomes a sprixel plane. it
// must not be used with other output mechanisms unless erased. the plane will
// be shrunk to fit the output, and the output is always placed at the origin.
// sprixels cannot be blitted to the standard plane.
//
// the placey/placex arguments thus refer to the position of the *plane*, not
// the sprixel. if creating a new plane, they will be used to place it. if
// using an existing plane, the plane will be moved. they are interpreted
// relative to the parent plane, as they would be in ncplane_create().
//
// by the end, disppixy/disppixx refer to the number of target rows/cols (in
// pixels), aka the scaled geometry. outy refers to the output height, subject
// to Sixel considerations. leny/lenx refer to the number of source rows/cols
// (likewise in pixels). begy/begx refer to the starting offset within the
// source. the sum of begy+leny must not exceed ncv->rows; the sum of begx+lenx
// must not exceed ncv->cols. these sums define the selected geometry. the
// output width is always equal to the scaled width; it has no distinct name.
ncplane* ncvisual_render_pixels(notcurses* nc, ncvisual* ncv, const struct blitset* bset,
                                int placey, int placex, const ncvgeom* geom,
                                ncplane* n, uint64_t flags, uint32_t transcolor,
                                int pxoffy, int pxoffx){
  logdebug("pblit: rows/cols: %dx%d plane: %d/%d", geom->rcelly, geom->rcellx, ncplane_dim_y(n), ncplane_dim_x(n));
  const tinfo* ti = &nc->tcache;
  blitterargs bargs;
  bargs.transcolor = transcolor;
  bargs.begy = geom->begy;
  bargs.begx = geom->begx;
  bargs.leny = geom->leny;
  bargs.lenx = geom->lenx;
  bargs.flags = flags;
  bargs.u.pixel.colorregs = ti->color_registers;
  bargs.u.pixel.pxoffy = pxoffy;
  bargs.u.pixel.pxoffx = pxoffx;
  bargs.u.pixel.cellpxy = geom->cdimy;
  bargs.u.pixel.cellpxx = geom->cdimx;
  const ncpile* p = ncplane_pile_const(n);
  if(n->sprite == NULL){
    if((n->sprite = sprixel_alloc(n, geom->rcelly, geom->rcellx)) == NULL){
      return NULL;
    }
    if((n->tam = create_tam(geom->rcelly, geom->rcellx)) == NULL){
      return NULL;;
    }
  }else{
    n->sprite = sprixel_recycle(n);
    if(n->sprite->dimy != geom->rcelly || n->sprite->dimx != geom->rcellx){
      destroy_tam(n);
      if((n->tam = create_tam(geom->rcelly, geom->rcellx)) == NULL){
        return NULL;
      }
    }
    n->sprite->dimx = geom->rcellx;
    n->sprite->dimy = geom->rcelly;
  }
  bargs.u.pixel.spx = n->sprite;
  // FIXME need to pull off the ncpile's sprixellist if anything below fails!
  if(ncvisual_blit_internal(ncv, geom->rpixy, geom->rpixx, n, bset, &bargs)){
    return NULL;
  }
  // if we created the plane earlier, placex/placey were taken into account, and
  // zeroed out, thus neither of these will have any effect.
  if(flags & NCVISUAL_OPTION_HORALIGNED){
    if(placex == NCALIGN_CENTER){
      placex = (ncplane_dim_x(ncplane_parent_const(n)) * p->cellpxx - geom->rpixx) / 2 / p->cellpxx;
    }else if(placex == NCALIGN_RIGHT){
      placex = (ncplane_dim_x(ncplane_parent_const(n)) * p->cellpxx - geom->rpixx) / p->cellpxx;
    }
    if(placex < 0){
      return NULL;
    }
  }
  if(flags & NCVISUAL_OPTION_VERALIGNED){
    if(placey == NCALIGN_CENTER){
      placey = (ncplane_dim_y(ncplane_parent_const(n)) * p->cellpxy - geom->rpixy) / 2 / p->cellpxy;
    }else if(placey == NCALIGN_BOTTOM){
      placey = (ncplane_dim_y(ncplane_parent_const(n)) * p->cellpxy - geom->rpixy) / p->cellpxy;
    }
    if(placey < 0){
      return NULL;
    }
  }
  // ncplane_resize() hides any attached sprixel, so lift it (the sprixel) out
  // for a moment as we shrink the plane to fit. we keep the origin and move to
  // the intended location.
  sprixel* s = n->sprite;
  n->sprite = NULL;
//fprintf(stderr, "ABOUT TO RESIZE: yoff/xoff: %d/%d\n",  placey, placex);
  // FIXME might need shrink down the TAM and kill unnecessary auxvecs
  if(ncplane_resize(n, 0, 0, s->dimy, s->dimx, placey, placex, s->dimy, s->dimx)){
    // if we blow up here, then we've got a TAM sized to the sprixel, rather
    // than the plane. running it through destroy_tam() via ncplane_destroy()
    // will use incorrect bounds for scrubbing said TAM. do it manually here.
    cleanup_tam(n->tam, geom->rcelly, geom->rcellx);
    free(n->tam);
    n->tam = NULL;
    sprixel_hide(bargs.u.pixel.spx);
    return NULL;
  }
  n->sprite = bargs.u.pixel.spx;
//fprintf(stderr, "RESIZED: %d/%d at %d/%d %p\n", ncplane_dim_y(n), ncplane_dim_x(n), ncplane_y(n), ncplane_x(n), n->sprite);
  return n;
}

ncplane* ncvisual_blit(notcurses* nc, ncvisual* ncv, const struct ncvisual_options* vopts){
//fprintf(stderr, "%p tacache: %p\n", n, n->tacache);
  struct ncvisual_options fakevopts;
  if(vopts == NULL){
    memset(&fakevopts, 0, sizeof(fakevopts));
    vopts = &fakevopts;
  }
  loginfo("inblit %dx%d %d@%d %dx%d @ %dx%d %p", ncv->pixy, ncv->pixx, vopts->y, vopts->x,
          vopts->leny, vopts->lenx, vopts->begy, vopts->begx, vopts->n);
  ncvgeom geom;
  const struct blitset* bset;
  unsigned disppxy, disppxx, outy, outx;
  int placey, placex;
  if(ncvisual_geom_inner(&nc->tcache, ncv, vopts, &geom, &bset,
                         &disppxy, &disppxx, &outy, &outx,
                         &placey, &placex)){
    // ncvisual_blitset_geom() emits its own diagnostics, no need for an error here
    return NULL;
  }
  ncplane* n = vopts->n;
  uint32_t transcolor = 0;
  if(vopts->flags & NCVISUAL_OPTION_ADDALPHA){
    transcolor = 0x1000000ull | vopts->transcolor;
  }
  ncplane* createdn = NULL; // to destroy on error
  if(n == NULL || (vopts->flags & NCVISUAL_OPTION_CHILDPLANE)){ // create plane
    struct ncplane_options nopts = {
      .y = placey,
      .x = placex,
      .rows = geom.rcelly,
      .cols = geom.rcellx,
      .userptr = NULL,
      .name = geom.blitter == NCBLIT_PIXEL ? "bmap" : "cvis",
      .resizecb = NULL,
      .flags = 0,
    };
    if(vopts->flags & NCVISUAL_OPTION_HORALIGNED){
      nopts.flags |= NCPLANE_OPTION_HORALIGNED;
      nopts.x = vopts->x;
    }
    if(vopts->flags & NCVISUAL_OPTION_VERALIGNED){
      nopts.flags |= NCPLANE_OPTION_VERALIGNED;
      nopts.y = vopts->y;
    }
    loginfo("placing new plane: %d/%d @ %d/%d 0x%016" PRIx64, nopts.rows, nopts.cols, nopts.y, nopts.x, nopts.flags);
    if(n == NULL){
      n = ncpile_create(nc, &nopts);
    }else{
      n = ncplane_create(n, &nopts);
    }
    if((createdn = n) == NULL){
      return NULL;
    }
    placey = 0;
    placex = 0;
  }
  logdebug("blit to plane %p at %d/%d geom %dx%d", n, ncplane_abs_y(n), ncplane_abs_x(n), ncplane_dim_y(n), ncplane_dim_x(n));
  if(geom.blitter != NCBLIT_PIXEL){
    n = ncvisual_render_cells(ncv, bset, placey, placex,
                              &geom, n, vopts->flags, transcolor);
  }else{
    n = ncvisual_render_pixels(nc, ncv, bset, placey, placex,
                               &geom, n,
                               vopts->flags, transcolor,
                               vopts->pxoffy, vopts->pxoffx);
  }
  if(n == NULL){
    ncplane_destroy(createdn);
  }
  return n;
}

ncvisual* ncvisual_from_plane(const ncplane* n, ncblitter_e blit,
                              int begy, int begx,
                              unsigned leny, unsigned lenx){
  unsigned py, px;
  uint32_t* rgba = ncplane_as_rgba(n, blit, begy, begx, leny, lenx, &py, &px);
//fprintf(stderr, "snarg: %d/%d @ %d/%d (%p)\n", leny, lenx, begy, begx, rgba);
  if(rgba == NULL){
    return NULL;
  }
  unsigned dimy, dimx;
  ncplane_dim_yx(n, &dimy, &dimx);
  ncvisual* ncv = ncvisual_from_rgba(rgba, py, px * 4, px);
  free(rgba);
//fprintf(stderr, "RETURNING %p\n", ncv);
  return ncv;
}

void ncvisual_destroy(ncvisual* ncv){
  if(ncv){
    if(visual_implementation->visual_destroy == NULL){
      if(ncv->owndata){
        free(ncv->data);
      }
      free(ncv);
    }else{
      visual_implementation->visual_destroy(ncv);
    }
  }
}

int ncvisual_simple_streamer(ncvisual* ncv, struct ncvisual_options* vopts,
                             const struct timespec* tspec, void* curry){
  struct ncplane* subtitle = NULL;
  int ret = 0;
  if(curry){
    // FIXME improve this hrmmmmm
    ncplane* subncp = curry;
    if(subncp->blist){
      ncplane_destroy(subncp->blist);
      subncp->blist = NULL;
    }
    subtitle = ncvisual_subtitle_plane(subncp, ncv);
  }
  if(notcurses_render(ncplane_notcurses(vopts->n))){
    return -1;
  }
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, tspec, NULL);
  ncplane_destroy(subtitle);
  return ret;
}

int ncvisual_set_yx(const struct ncvisual* n, unsigned y, unsigned x, uint32_t pixel){
  if(y >= n->pixy){
    logerror("invalid coordinates %u/%u", y, x);
    return -1;
  }
  if(x >= n->pixx){
    logerror("invalid coordinates %u/%u", y, x);
    return -1;
  }
  n->data[y * (n->rowstride / 4) + x] = pixel;
  return 0;
}

int ncvisual_at_yx(const ncvisual* n, unsigned y, unsigned x, uint32_t* pixel){
  if(y >= n->pixy){
    logerror("invalid coordinates %u/%u (%d/%d)", y, x, n->pixy, n->pixx);
    return -1;
  }
  if(x >= n->pixx){
    logerror("invalid coordinates %u/%u (%d/%d)", y, x, n->pixy, n->pixx);
    return -1;
  }
  *pixel = n->data[y * (n->rowstride / 4) + x];
  return 0;
}

// originally i wrote this recursively, at which point it promptly began
// exploding once i multithreaded the [yield] demo. hence the clumsy stack
// and hand-rolled iteration. alas, poor yorick!
static int
ncvisual_polyfill_core(ncvisual* n, unsigned y, unsigned x, uint32_t rgba, uint32_t match){
  struct topolyfill* stack = malloc(sizeof(*stack));
  if(stack == NULL){
    return -1;
  }
  stack->y = y;
  stack->x = x;
  stack->next = NULL;
  int ret = 0;
  struct topolyfill* s;
  do{
    s = stack;
    stack = s->next;
    y = s->y;
    x = s->x;
    uint32_t* pixel = &n->data[y * (n->rowstride / 4) + x];
    if(*pixel == match && *pixel != rgba){
      ++ret;
    // fprintf(stderr, "%d/%d: setting %08x to %08x\n", y, x, *pixel, rgba);
      *pixel = rgba;
      if(y){
        if(create_polyfill_op(y - 1, x, &stack) == NULL){
          goto err;
        }
      }
      if(y + 1 < n->pixy){
        if(create_polyfill_op(y + 1, x, &stack) == NULL){
          goto err;
        }
      }
      if(x){
        if(create_polyfill_op(y, x - 1, &stack) == NULL){
          goto err;
        }
      }
      if(x + 1 < n->pixx){
        if(create_polyfill_op(y, x + 1, &stack) == NULL){
          goto err;
        }
      }
    }
    free(s);
  }while(stack);
  return ret;

err:
  free(s);
  while(stack){
    s = stack->next;
    free(stack);
    stack = s;
  }
  return -1;
}

int ncvisual_polyfill_yx(ncvisual* n, unsigned y, unsigned x, uint32_t rgba){
  if(y >= n->pixy){
    logerror("invalid coordinates %u/%u", y, x);
    return -1;
  }
  if(x >= n->pixx){
    logerror("invalid coordinates %u/%u", y, x);
    return -1;
  }
  uint32_t* pixel = &n->data[y * (n->rowstride / 4) + x];
  return ncvisual_polyfill_core(n, y, x, rgba, *pixel);
}

bool notcurses_canopen_images(const notcurses* nc __attribute__ ((unused))){
  if(!visual_implementation->canopen_images){
    return false;
  }
  return visual_implementation->canopen_images;
}

bool notcurses_canopen_videos(const notcurses* nc __attribute__ ((unused))){
  if(!visual_implementation->canopen_videos){
    return false;
  }
  return visual_implementation->canopen_videos;
}
