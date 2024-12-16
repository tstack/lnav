#include <math.h>
#include <float.h>
#include <limits.h>
#include <string.h>
#include <inttypes.h>
#include "internal.h"

// common elements of type-parameterized plots
typedef struct ncplot {
  ncplane* ncp;
  ncplane* pixelp; // only used for NCBLIT_PIXEL
  /* sloutcount-element circular buffer of samples. the newest one (rightmost)
     is at slots[slotstart]; they get older as you go back (and around).
     elements. slotcount is max(columns, rangex), less label room. */
  int64_t slotx; /* x value corresponding to slots[slotstart] (newest x) */
  uint64_t maxchannels;
  uint64_t minchannels;
  uint16_t legendstyle;
  bool vertical_indep; /* not yet implemented FIXME */
  unsigned chancount; // channel count (can change on cell-pixel geom change)
  uint64_t* channels; // computed in calculate_gradient_vector() (constructor)
  const struct blitset* bset;
  char* title;
  /* requested number of slots. 0 for automatically setting the number of slots
     to span the horizontal area. if there are more slots than there are
     columns, we prefer showing more recent slots to less recent. if there are
     fewer slots than there are columns, they prefer the left side. */
  unsigned rangex;
  /* domain minimum and maximum. if detectdomain is true, these are
     progressively enlarged/shrunk to fit the sample set. if not, samples
     outside these bounds are counted, but the displayed range covers only this. */
  unsigned slotcount;
  int slotstart; /* index of most recently-written slot */
  bool labelaxisd; /* label dependent axis (consumes NCPREFIXCOLUMNS columns) */
  bool exponentiali; /* exponential independent axis */
  bool detectdomain; /* is domain detection in effect (stretch the domain)? */
  bool detectonlymax; /* domain detection applies only to max, not min */
  bool printsample; /* print the most recent sample */
} ncplot;

static inline int
create_pixelp(ncplot *p, ncplane* n){
  if(((p->pixelp = ncplane_dup(n, NULL)) == NULL)){
    return -1;
  }
  if(ncplane_set_name(p->pixelp, "pmap")){
    ncplane_destroy(p->pixelp);
    return -1;
  }
  ncplane_reparent(p->pixelp, n);
  ncplane_move_below(p->pixelp, n);
  uint64_t basechan = 0;
  ncchannels_set_bg_alpha(&basechan, NCALPHA_TRANSPARENT);
  ncchannels_set_fg_alpha(&basechan, NCALPHA_TRANSPARENT);
  ncplane_set_base(n, "", 0, basechan);
  return 0;
}

// we have some color gradient across the life of the plot (almost; it gets
// recalculated if the cell-pixel geometry changes and we're using
// NCBLIT_PIXEL). if we're using cell blitting, we only get one channel pair
// per row, no matter what height we have. with pixels, we get cellpxy * rows.
static int
calculate_gradient_vector(ncplot* p, unsigned pixelp){
  const int dimy = ncplane_dim_y(p->ncp);
  const unsigned states = dimy * (pixelp ? ncplane_pile(p->ncp)->cellpxy : 1);
  if(states == p->chancount){ // no need to recalculate
    return 0;
  }
  uint64_t* tmp = realloc(p->channels, states * sizeof(*p->channels));
  if(tmp == NULL){
    return -1;
  }
  p->channels = tmp;
  p->chancount = states;
  for(unsigned y = 0 ; y < p->chancount ; ++y){ \
    calc_gradient_channels(&p->channels[y], p->minchannels, p->minchannels,
                           p->maxchannels, p->maxchannels,
                           y, 0, p->chancount, 0);
  }
  return 0;
}

#define MAXWIDTH 2
#define CREATE(T, X) \
typedef struct nc##X##plot { \
  T* slots; \
  T miny, maxy; \
  ncplot plot; \
} nc##X##plot; \
\
static int redraw_pixelplot_##T(nc##X##plot* ncp){ \
  if(calculate_gradient_vector(&ncp->plot, 1)){ \
    return -1; \
  } \
  const int scale = ncplane_pile_const(ncp->plot.ncp)->cellpxx; \
  ncplane_erase(ncp->plot.ncp); \
  unsigned dimy, dimx; \
  ncplane_dim_yx(ncp->plot.ncp, &dimy, &dimx); \
  const unsigned scaleddim = dimx * scale; \
  /* each transition is worth this much change in value */ \
  const size_t states = ncplane_pile_const(ncp->plot.ncp)->cellpxy; \
  /* FIXME can we not rid ourselves of this meddlesome double? either way, the \
     interval is one row's range (for linear plots), or the base \
     (base^slots == maxy-miny) of the range (for exponential plots). */ \
  double interval; \
  if(ncp->plot.exponentiali){ \
    if(ncp->maxy > ncp->miny){ \
      interval = pow(ncp->maxy - ncp->miny, (double)1 / (dimy * states)); \
/* fprintf(stderr, "miny: %ju maxy: %ju dimy: %d states: %zu\n", miny, maxy, dimy, states); */ \
    }else{ \
      interval = 0; \
    } \
  }else{ \
    interval = ncp->maxy < ncp->miny ? 0 : (ncp->maxy - ncp->miny) / ((double)dimy * states); \
  } \
  const int startx = ncp->plot.labelaxisd ? NCPREFIXCOLUMNS : 0; /* plot cols begin here */ \
  /* if we want fewer slots than there are available columns, our final column \
     will be other than the plane's final column. most recent x goes here. */ \
  const unsigned finalx = (ncp->plot.slotcount < scaleddim - 1 - (startx * scale) ? \
                          startx + (ncp->plot.slotcount / scale) - 1 : dimx - 1); \
  ncplane_set_styles(ncp->plot.ncp, ncp->plot.legendstyle); \
  if(ncp->plot.labelaxisd){ \
    /* show the *top* of each interval range */ \
    for(unsigned y = 0 ; y < dimy ; ++y){ \
      ncplane_set_channels(ncp->plot.ncp, ncp->plot.channels[y * states]); \
      char buf[NCPREFIXSTRLEN + 1]; \
      if(ncp->plot.exponentiali){ \
        if(y == dimy - 1){ /* we cheat on the top row to exactly match maxy */ \
          ncqprefix(ncp->maxy * 100, 100, buf, 0); \
        }else{ \
          ncqprefix(pow(interval, (y + 1) * states) * 100, 100, buf, 0); \
        } \
      }else{ \
        ncqprefix((ncp->maxy - interval * states * (dimy - y - 1)) * 100, 100, buf, 0); \
      } \
      if(y == dimy - 1 && strlen(ncp->plot.title)){ \
        ncplane_printf_yx(ncp->plot.ncp, dimy - y - 1, 0, "%*.*s %s", \
                          NCPREFIXSTRLEN, NCPREFIXSTRLEN, buf, ncp->plot.title); \
      }else{ \
        ncplane_printf_yx(ncp->plot.ncp, dimy - y - 1, 0, "%*.*s", \
                          NCPREFIXSTRLEN, NCPREFIXSTRLEN, buf); \
      } \
    } \
  }else if(strlen(ncp->plot.title)){ \
    ncplane_set_channels(ncp->plot.ncp, ncp->plot.channels[(dimy - 1) * states]); \
    ncplane_printf_yx(ncp->plot.ncp, 0, NCPREFIXCOLUMNS - strlen(ncp->plot.title), "%s", ncp->plot.title); \
  } \
  ncplane_set_styles(ncp->plot.ncp, NCSTYLE_NONE); \
  if((int)finalx < startx){ /* exit on pathologically narrow planes */ \
    return 0; \
  } \
  if(!interval){ \
    interval = 1; \
  } \
  uint32_t* pixels = malloc(dimy * dimx * states * scale * sizeof(*pixels)); \
  if(pixels == NULL){ \
    return -1; \
  } \
  /* FIXME just zero out as we copy to each */ \
  memset(pixels, 0, dimy * dimx * states * scale * sizeof(*pixels)); \
  /* a column corresponds to |scale| slots' worth of samples. prepare the working gval set. */ \
  T* gvals = malloc(sizeof(*gvals) * scale); \
  if(gvals == NULL){ \
    free(pixels); \
    return -1; \
  } \
  int idx = ncp->plot.slotstart; /* idx holds the real slot index; we move backwards */ \
  /* iterate backwards across the plot from the final (rightmost) x being \
     plotted (finalx) to the first (leftmost) x being plotted (startx).   */ \
  for(int x = finalx ; x >= startx ; --x){ \
    /* load gvals retaining the same ordering we have in the actual array */ \
    for(int i = scale - 1 ; i >= 0 ; --i){ \
      gvals[i] = ncp->slots[idx]; /* clip the value at the limits of the graph */ \
      if(gvals[i] < ncp->miny){ \
        gvals[i] = ncp->miny; \
      } \
      if(gvals[i] > ncp->maxy){ \
        gvals[i] = ncp->maxy; \
      } \
      /* FIXME if there are an odd number, only go up through the valid ones... */ \
      if(--idx < 0){ \
        idx = ncp->plot.slotcount - 1; \
      } \
    } \
    /* starting from the least-significant row, progress in the more significant \
       direction, prepping pixels, aborting early if we can't draw anything in a \
       given cell. */ \
    T intervalbase = ncp->miny; \
    bool done = !ncp->plot.bset->fill; \
    for(unsigned y = 0 ; y < dimy ; ++y){ \
      /* if we've got at least one interval's worth on the number of positions \
        times the number of intervals per position plus the starting offset, \
        we're going to print *something* */ \
      for(int i = 0 ; i < scale ; ++i){ \
        size_t egcidx; \
        if(intervalbase < gvals[i]){ \
          if(ncp->plot.exponentiali){ \
            /* we want the log-base-interval of gvals[i] */ \
            double scaled = log(gvals[i] - ncp->miny) / log(interval); \
            double sival = intervalbase ? log(intervalbase) / log(interval) : 0; \
            egcidx = scaled - sival; \
          }else{ \
            egcidx = (gvals[i] - intervalbase) / interval; \
          } \
          if(egcidx >= states){ \
            egcidx = states; \
            done = false; \
          } \
        }else{ \
          egcidx = 0; \
        } \
/*fprintf(stderr, "WRITING TO y/x %d/%d (%zu)\n", y, x, dimx * dimy * scale * states); */\
        for(size_t yy = 0 ; yy < egcidx ; ++yy){ \
          int poff = x * scale + i + (((dimy - 1 - y) * states + (states - 1 - yy)) * dimx * scale); \
          uint32_t color = ncchannels_fg_rgb(ncp->plot.channels[y * states + yy]); \
          ncpixel_set_a(&color, 0xff); \
          pixels[poff] = color; \
        } \
      } \
      if(done){ \
        break; \
      } \
      if(ncp->plot.exponentiali){ \
        intervalbase = ncp->miny + pow(interval, (y + 1) * states - 1); \
      }else{ \
        intervalbase += (states * interval); \
      } \
    } \
  } \
  if(ncp->plot.printsample){ \
    ncplane_set_styles(ncp->plot.ncp, ncp->plot.legendstyle); \
    ncplane_set_channels(ncp->plot.ncp, ncp->plot.maxchannels); \
    /* FIXME is this correct for double? */ \
    /* we use idx, and thus get an immediate count, changing as we load it.
     * if you want a stable summary, print the previous slot */ \
    ncplane_printf_aligned(ncp->plot.ncp, 0, NCALIGN_RIGHT, "%" PRIu64, (uint64_t)ncp->slots[idx]); \
  } \
  ncplane_home(ncp->plot.ncp); \
  struct ncvisual* ncv = ncvisual_from_rgba(pixels, dimy * states, dimx * scale * 4, dimx * scale); \
  free(pixels); \
  free(gvals); \
  if(ncv == NULL){ \
    return -1; \
  } \
  struct ncvisual_options vopts = { \
    .n = ncp->plot.pixelp, \
    .blitter = NCBLIT_PIXEL, \
    .flags = NCVISUAL_OPTION_NODEGRADE, \
  }; \
  if(ncvisual_blit(ncplane_notcurses(ncp->plot.ncp), ncv, &vopts) == NULL){ \
    ncvisual_destroy(ncv); \
    return -1; \
  } \
  ncvisual_destroy(ncv); \
  return 0; \
} \
\
static int redraw_plot_##T(nc##X##plot* ncp){ \
  if(ncp->plot.bset->geom == NCBLIT_PIXEL){ \
    return redraw_pixelplot_##T(ncp); \
  } \
  if(calculate_gradient_vector(&ncp->plot, 0)){ \
    return -1; \
  } \
  ncplane_erase(ncp->plot.ncp); \
  const unsigned scale = ncp->plot.bset->width; \
  unsigned dimy, dimx; \
  ncplane_dim_yx(ncp->plot.ncp, &dimy, &dimx); \
  const unsigned scaleddim = dimx * scale; \
  /* each transition is worth this much change in value */ \
  const size_t states = ncp->plot.bset->height + 1; \
  /* FIXME can we not rid ourselves of this meddlesome double? either way, the \
     interval is one row's range (for linear plots), or the base \
     (base^slots == maxy-miny) of the range (for exponential plots). */ \
  double interval; \
  if(ncp->plot.exponentiali){ \
    if(ncp->maxy > ncp->miny){ \
      interval = pow(ncp->maxy - ncp->miny, (double)1 / (dimy * states)); \
/* fprintf(stderr, "miny: %ju maxy: %ju dimy: %d states: %zu\n", miny, maxy, dimy, states); */ \
    }else{ \
      interval = 0; \
    } \
  }else{ \
    interval = ncp->maxy < ncp->miny ? 0 : (ncp->maxy - ncp->miny) / ((double)dimy * states); \
  } \
  const int startx = ncp->plot.labelaxisd ? NCPREFIXCOLUMNS : 0; /* plot cols begin here */ \
  /* if we want fewer slots than there are available columns, our final column \
     will be other than the plane's final column. most recent x goes here. */ \
  const unsigned finalx = (ncp->plot.slotcount < scaleddim - 1 - (startx * scale) ? \
                          startx + (ncp->plot.slotcount / scale) - 1 : dimx - 1); \
  ncplane_set_styles(ncp->plot.ncp, ncp->plot.legendstyle); \
  if(ncp->plot.labelaxisd){ \
    /* show the *top* of each interval range */ \
    for(unsigned y = 0 ; y < dimy ; ++y){ \
      ncplane_set_channels(ncp->plot.ncp, ncp->plot.channels[y]); \
      char buf[NCPREFIXSTRLEN + 1]; \
      if(ncp->plot.exponentiali){ \
        if(y == dimy - 1){ /* we cheat on the top row to exactly match maxy */ \
          ncqprefix(ncp->maxy * 100, 100, buf, 0); \
        }else{ \
          ncqprefix(pow(interval, (y + 1) * states) * 100, 100, buf, 0); \
        } \
      }else{ \
        ncqprefix((ncp->maxy - interval * states * (dimy - y - 1)) * 100, 100, buf, 0); \
      } \
      if(y == dimy - 1 && strlen(ncp->plot.title)){ \
        ncplane_printf_yx(ncp->plot.ncp, dimy - y - 1, NCPREFIXCOLUMNS - strlen(buf), "%s %s", buf, ncp->plot.title); \
      }else{ \
        ncplane_printf_yx(ncp->plot.ncp, dimy - y - 1, NCPREFIXCOLUMNS - strlen(buf), "%s", buf); \
      } \
    } \
  }else if(strlen(ncp->plot.title)){ \
    ncplane_set_channels(ncp->plot.ncp, ncp->plot.channels[dimy - 1]); \
    ncplane_printf_yx(ncp->plot.ncp, 0, NCPREFIXCOLUMNS - strlen(ncp->plot.title), "%s", ncp->plot.title); \
  } \
  ncplane_set_styles(ncp->plot.ncp, NCSTYLE_NONE); \
  if((int)finalx < startx){ /* exit on pathologically narrow planes */ \
    return 0; \
  } \
  if(!interval){ \
    interval = 1; \
  } \
  int idx = ncp->plot.slotstart; /* idx holds the real slot index; we move backwards */ \
  for(int x = finalx ; x >= startx ; --x){ \
    /* a single column might correspond to more than 1 ('scale', up to \
       MAXWIDTH) slots' worth of samples. prepare the working gval set. */ \
    T gvals[MAXWIDTH]; \
    /* load it retaining the same ordering we have in the actual array */ \
    for(int i = scale - 1 ; i >= 0 ; --i){ \
      gvals[i] = ncp->slots[idx]; /* clip the value at the limits of the graph */ \
      if(gvals[i] < ncp->miny){ \
        gvals[i] = ncp->miny; \
      } \
      if(gvals[i] > ncp->maxy){ \
        gvals[i] = ncp->maxy; \
      } \
      /* FIXME if there are an odd number, only go up through the valid ones... */ \
      if(--idx < 0){ \
        idx = ncp->plot.slotcount - 1; \
      } \
    } \
    /* starting from the least-significant row, progress in the more significant \
       direction, drawing egcs from the grid specification, aborting early if \
       we can't draw anything in a given cell. */ \
    T intervalbase = ncp->miny; \
    const wchar_t* egc = ncp->plot.bset->plotegcs; \
    bool done = !ncp->plot.bset->fill; \
    for(unsigned y = 0 ; y < dimy ; ++y){ \
      ncplane_set_channels(ncp->plot.ncp, ncp->plot.channels[y]); \
      size_t egcidx = 0, sumidx = 0; \
      /* if we've got at least one interval's worth on the number of positions \
        times the number of intervals per position plus the starting offset, \
        we're going to print *something* */ \
      for(unsigned i = 0 ; i < scale ; ++i){ \
        sumidx *= states; \
        if(intervalbase < gvals[i]){ \
          if(ncp->plot.exponentiali){ \
            /* we want the log-base-interval of gvals[i] */ \
            double scaled = log(gvals[i] - ncp->miny) / log(interval); \
            double sival = intervalbase ? log(intervalbase) / log(interval) : 0; \
            egcidx = scaled - sival; \
          }else{ \
            egcidx = (gvals[i] - intervalbase) / interval; \
          } \
          if(egcidx >= states){ \
            egcidx = states - 1; \
            done = false; \
          } \
          sumidx += egcidx; \
        }else{ \
          egcidx = 0; \
        } \
/* printf(stderr, "y: %d i(scale): %d gvals[%d]: %ju egcidx: %zu sumidx: %zu interval: %f intervalbase: %ju\n", y, i, i, gvals[i], egcidx, sumidx, interval, intervalbase); */ \
      } \
      /* if we're not UTF8, we can only arrive here via NCBLIT_1x1 (otherwise \
        we would have errored out during construction). even then, however, \
        we need handle ASCII differently, since it can't print full block. \
        in ASCII mode, sumidx != 0 means swap colors and use space. in all \
        modes, sumidx == 0 means don't do shit, since we erased earlier. */ \
/* if(sumidx)fprintf(stderr, "dimy: %d y: %d x: %d sumidx: %zu egc[%zu]: %lc\n", dimy, y, x, sumidx, sumidx, egc[sumidx]); */ \
      if(sumidx){ \
        uint64_t chan = ncp->plot.channels[y]; \
        if(notcurses_canutf8(ncplane_notcurses(ncp->plot.ncp))){ \
          char utf8[MB_LEN_MAX + 1]; \
          int bytes = wctomb(utf8, egc[sumidx]); \
          if(bytes < 0){ \
            return -1; \
          } \
          utf8[bytes] = '\0'; \
          nccell* c = ncplane_cell_ref_yx(ncp->plot.ncp, dimy - y - 1, x); \
          cell_set_bchannel(c, ncchannels_bchannel(chan)); \
          cell_set_fchannel(c, ncchannels_fchannel(chan)); \
          nccell_set_styles(c, NCSTYLE_NONE); \
          if(pool_blit_direct(&ncp->plot.ncp->pool, c, utf8, bytes, 1) <= 0){ \
            return -1; \
          } \
        }else{ \
          const uint64_t swapbg = ncchannels_bchannel(chan); \
          const uint64_t swapfg = ncchannels_fchannel(chan); \
          ncchannels_set_bchannel(&chan, swapfg); \
          ncchannels_set_fchannel(&chan, swapbg); \
          ncplane_set_channels(ncp->plot.ncp, chan); \
          if(ncplane_putchar_yx(ncp->plot.ncp, dimy - y - 1, x, ' ') <= 0){ \
            return -1; \
          } \
          ncchannels_set_bchannel(&chan, swapbg); \
          ncchannels_set_fchannel(&chan, swapfg); \
          ncplane_set_channels(ncp->plot.ncp, chan); \
        } \
      } \
      if(done){ \
        break; \
      } \
      if(ncp->plot.exponentiali){ \
        intervalbase = ncp->miny + pow(interval, (y + 1) * states - 1); \
      }else{ \
        intervalbase += (states * interval); \
      } \
    } \
  } \
  if(ncp->plot.printsample){ \
    ncplane_set_styles(ncp->plot.ncp, ncp->plot.legendstyle); \
    ncplane_set_channels(ncp->plot.ncp, ncp->plot.maxchannels); \
    ncplane_printf_aligned(ncp->plot.ncp, 0, NCALIGN_RIGHT, "%" PRIu64, (uint64_t)ncp->slots[idx]); \
  } \
  ncplane_home(ncp->plot.ncp); \
  return 0; \
} \
\
static const struct blitset* \
create_##T(nc##X##plot* ncpp, ncplane* n, const ncplot_options* opts, \
           const T miny, const T maxy, const T trueminy, const T truemaxy){ \
  /* set up ->plot.ncp first so it gets destroyed on error */ \
  ncpp->plot.ncp = n; \
  if(ncplane_set_widget(ncpp->plot.ncp, ncpp, (void(*)(void*))nc##X##plot_destroy)){ \
    return NULL; \
  } \
  ncplot_options zeroed = {0}; \
  if(!opts){ \
    opts = &zeroed; \
  } \
  if(opts->flags >= (NCPLOT_OPTION_PRINTSAMPLE << 1u)){ \
    logwarn("provided unsupported flags %016" PRIx64, opts->flags); \
  } \
  /* if miny == maxy (enabling domain detection), they both must be equal to 0 */ \
  if(miny == maxy && miny){ \
    return NULL; \
  } \
  if(opts->rangex < 0){ \
    logerror("error: supplied negative independent range %d", opts->rangex); \
    return NULL; \
  } \
  if(maxy < miny){ \
    logerror("error: supplied maxy < miny"); \
    return NULL; \
  } \
  /* DETECTMAXONLY can't be used without domain detection */ \
  if(opts->flags & NCPLOT_OPTION_DETECTMAXONLY && (miny != maxy)){ \
    logerror("supplied DETECTMAXONLY without domain detection"); \
    return NULL; \
  } \
  const notcurses* notc = ncplane_notcurses(n); \
  ncblitter_e blitfxn = opts ? opts->gridtype : NCBLIT_DEFAULT; \
  if(blitfxn == NCBLIT_DEFAULT){ \
    blitfxn = ncplot_defblitter(notc); \
  } \
  bool degrade_blitter = !(opts && (opts->flags & NCPLOT_OPTION_NODEGRADE)); \
  const struct blitset* bset = lookup_blitset(&notc->tcache, blitfxn, degrade_blitter); \
  if(bset == NULL){ \
    return NULL; \
  } \
  unsigned sdimy, sdimx; \
  ncplane_dim_yx(n, &sdimy, &sdimx); \
  if(sdimx <= 0){ \
    return NULL; \
  } \
  unsigned dimx = sdimx; \
  ncpp->plot.title = strdup(opts->title ? opts->title : ""); \
  ncpp->plot.rangex = opts->rangex; \
  /* if we're sizing the plot based off the plane dimensions, scale it by the \
     plot geometry's width for all calculations */ \
  const unsigned scaleddim = dimx * (bset->geom == NCBLIT_PIXEL ? ncplane_pile_const(n)->cellpxx : bset->width); \
  const unsigned scaledprefixlen = NCPREFIXCOLUMNS * (bset->geom == NCBLIT_PIXEL ? ncplane_pile_const(n)->cellpxx : bset->width); \
  if((ncpp->plot.slotcount = ncpp->plot.rangex) == 0){ \
    ncpp->plot.slotcount = scaleddim; \
  } \
  if(dimx < ncpp->plot.rangex){ \
    ncpp->plot.slotcount = scaleddim; \
  } \
  ncpp->plot.legendstyle = opts->legendstyle; \
  if( (ncpp->plot.labelaxisd = opts->flags & NCPLOT_OPTION_LABELTICKSD) ){ \
    if(ncpp->plot.slotcount + scaledprefixlen > scaleddim){ \
      if(scaleddim > scaledprefixlen){ \
        ncpp->plot.slotcount = scaleddim - scaledprefixlen; \
      } \
    } \
  } \
  size_t slotsize = sizeof(*ncpp->slots) * ncpp->plot.slotcount; \
  ncpp->slots = malloc(slotsize); \
  if(ncpp->slots == NULL){ \
    return NULL; \
  } \
  memset(ncpp->slots, 0, slotsize); \
  ncpp->plot.maxchannels = opts->maxchannels; \
  ncpp->plot.minchannels = opts->minchannels; \
  ncpp->plot.bset = bset; \
  ncpp->miny = miny; \
  ncpp->maxy = maxy; \
  ncpp->plot.vertical_indep = opts->flags & NCPLOT_OPTION_VERTICALI; \
  ncpp->plot.exponentiali = opts->flags & NCPLOT_OPTION_EXPONENTIALD; \
  ncpp->plot.detectonlymax = opts->flags & NCPLOT_OPTION_DETECTMAXONLY; \
  ncpp->plot.printsample = opts->flags & NCPLOT_OPTION_PRINTSAMPLE; \
  if( (ncpp->plot.detectdomain = (miny == maxy)) ){ \
    ncpp->maxy = trueminy; \
    if(!ncpp->plot.detectonlymax){ \
      ncpp->miny = truemaxy; \
    } \
  } \
  ncpp->plot.slotstart = 0; \
  ncpp->plot.slotx = 0; \
  ncpp->plot.chancount = 0; \
  ncpp->plot.channels = NULL; \
  if(bset->geom == NCBLIT_PIXEL){ \
    if(create_pixelp(&ncpp->plot, n)){ \
      return NULL; \
    } \
  } \
  redraw_plot_##T(ncpp); \
  return bset; \
} \
/* if x is less than the window, return -1, as the sample will be thrown away. \
   if the x is within the current window, find the proper slot and update it. \
   otherwise, the x is the newest sample. if it is obsoletes all existing slots, \
   reset them, and write the new sample anywhere. otherwise, write it to the \
   proper slot based on the current newest slot. */ \
int window_slide_##T(nc##X##plot* ncp, int64_t x){ \
  if(x <= ncp->plot.slotx){ /* x is within window, do nothing */ \
    return 0; \
  } /* x is newest; we might be keeping some, might not */ \
  int64_t xdiff = x - ncp->plot.slotx; /* the raw amount we're advancing */ \
  ncp->plot.slotx = x; \
  if(xdiff >= ncp->plot.slotcount){ /* we're throwing away all old samples, write to 0 */ \
    memset(ncp->slots, 0, sizeof(*ncp->slots) * ncp->plot.slotcount); \
    ncp->plot.slotstart = 0; \
    return 0; \
  } \
  /* we're throwing away only xdiff slots, which is less than slotcount. \
     first, we'll try to clear to the right...number to reset on the right of \
     the circular buffer. min of (available at current or to right, xdiff) */ \
  int slotsreset = ncp->plot.slotcount - ncp->plot.slotstart - 1; \
  if(slotsreset > xdiff){ \
    slotsreset = xdiff; \
  } \
  if(slotsreset){ \
    memset(ncp->slots + ncp->plot.slotstart + 1, 0, slotsreset * sizeof(*ncp->slots)); \
  } \
  ncp->plot.slotstart = (ncp->plot.slotstart + xdiff) % ncp->plot.slotcount; \
  xdiff -= slotsreset; \
  if(xdiff){ /* throw away some at the beginning */ \
    memset(ncp->slots, 0, xdiff * sizeof(*ncp->slots)); \
  } \
  return 0; \
} \
\
static int update_domain_##T(nc##X##plot* ncp, uint64_t x); \
static void update_sample_##T(nc##X##plot* ncp, int64_t x, T y, bool reset); \
\
/* Add to or set the value corresponding to this x. If x is beyond the current \
   x window, the x window is advanced to include x, and values passing beyond \
   the window are lost. The first call will place the initial window. The plot \
   will be redrawn, but notcurses_render() is not called. */ \
int add_sample_##T(nc##X##plot* ncpp, int64_t x, T y){ \
  if(x < ncpp->plot.slotx - (ncpp->plot.slotcount - 1)){ /* x is behind window, won't be counted */ \
    return -1; \
  } \
  if(y == 0 && x <= ncpp->plot.slotx){ \
    return 0; /* no need to redraw plot; nothing changed */ \
  } \
  if(window_slide_##T(ncpp, x)){ \
    return -1; \
  } \
  update_sample_##T(ncpp, x, y, false); \
  if(update_domain_##T(ncpp, x)){ \
    return -1; \
  } \
  return redraw_plot_##T(ncpp); \
} \
int sample_##T(const nc##X##plot* ncp, int64_t x, T* y){ \
  if(x < ncp->plot.slotx - (ncp->plot.slotcount - 1)){ /* x is behind window */ \
    return -1; \
  }else if(x > ncp->plot.slotx){ /* x is ahead of window */ \
    return -1; \
  } \
  *y = ncp->slots[x % ncp->plot.slotcount]; \
  return 0; \
}

CREATE(uint64_t, u)
CREATE(double, d)

static void
ncplot_destroy(ncplot* n){
  free(n->title);
  if(ncplane_set_widget(n->ncp, NULL, NULL) == 0){
    ncplane_destroy(n->ncp);
  }
  ncplane_destroy(n->pixelp);
  free(n->channels);
}

/* if we're doing domain detection, update the domain to reflect the value we
   just set. if we're not, check the result against the known ranges, and
   return -1 if the value is outside of that range. */
int update_domain_uint64_t(ncuplot* ncp, uint64_t x){
  const uint64_t val = ncp->slots[x % ncp->plot.slotcount];
  if(ncp->plot.detectdomain){
    if(val > ncp->maxy){
      ncp->maxy = val;
    }
    if(!ncp->plot.detectonlymax){
      if(val < ncp->miny){
        ncp->miny = val;
      }
    }
    return 0;
  }
  if(val > ncp->maxy || val < ncp->miny){
    return -1;
  }
  return 0;
}

int update_domain_double(ncdplot* ncp, uint64_t x){
  const double val = ncp->slots[x % ncp->plot.slotcount];
  if(ncp->plot.detectdomain){
    if(val > ncp->maxy){
      ncp->maxy = val;
    }
    if(!ncp->plot.detectonlymax){
      if(val < ncp->miny){
        ncp->miny = val;
      }
    }
    return 0;
  }
  if(val > ncp->maxy || val < ncp->miny){
    return -1;
  }
  return 0;
}

/* x must be within n's window at this point */
static void
update_sample_uint64_t(ncuplot* ncp, int64_t x, uint64_t y, bool reset){
  const int64_t diff = ncp->plot.slotx - x; /* amount behind */
  const int idx = (ncp->plot.slotstart + ncp->plot.slotcount - diff) % ncp->plot.slotcount;
  if(reset){
    ncp->slots[idx] = y;
  }else{
    ncp->slots[idx] += y;
  }
}

/* x must be within n's window at this point */
static void
update_sample_double(ncdplot* ncp, int64_t x, double y, bool reset){
  const int64_t diff = ncp->plot.slotx - x; /* amount behind */
  const int idx = (ncp->plot.slotstart + ncp->plot.slotcount - diff) % ncp->plot.slotcount;
  if(reset){
    ncp->slots[idx] = y;
  }else{
    ncp->slots[idx] += y;
  }
}

// takes ownership of n on all paths
ncuplot* ncuplot_create(ncplane* n, const ncplot_options* opts, uint64_t miny, uint64_t maxy){
  ncuplot* ret = malloc(sizeof(*ret));
  if(ret == NULL){
    ncplane_destroy(n);
    return NULL;
  }
  memset(ret, 0, sizeof(*ret));
  const struct blitset* bset = create_uint64_t(ret, n, opts, miny, maxy, 0, UINT64_MAX);
  if(bset == NULL){ // create_uint64_t() destroys n on error
    ncuplot_destroy(ret);
    return NULL;
  }
  return ret;
}

ncplane* ncuplot_plane(ncuplot* n){
  return n->plot.ncp;
}

int ncuplot_add_sample(ncuplot* n, uint64_t x, uint64_t y){
  return add_sample_uint64_t(n, x, y);
}

int ncuplot_set_sample(ncuplot* n, uint64_t x, uint64_t y){
  if(window_slide_uint64_t(n, x)){
    return -1;
  }
  update_sample_uint64_t(n, x, y, true);
  if(update_domain_uint64_t(n, x)){
    return -1;
  }
  return redraw_plot_uint64_t(n);
}

void ncuplot_destroy(ncuplot* n){
  if(n){
    ncplot_destroy(&n->plot);
    free(n->slots);
    free(n);
  }
}

// takes ownership of n on all paths
ncdplot* ncdplot_create(ncplane* n, const ncplot_options* opts, double miny, double maxy){
  ncdplot* ret = malloc(sizeof(*ret));
  if(ret == NULL){
    ncplane_destroy(n);
    return NULL;
  }
  memset(ret, 0, sizeof(*ret));
  const struct blitset* bset = create_double(ret, n, opts, miny, maxy, -DBL_MAX, DBL_MAX);
  if(bset == NULL){ // create_double() destroys n on error
    ncdplot_destroy(ret);
    return NULL;
  }
  return ret;
}

ncplane* ncdplot_plane(ncdplot* n){
  return n->plot.ncp;
}

int ncdplot_add_sample(ncdplot* n, uint64_t x, double y){
  return add_sample_double(n, x, y);
}

int ncdplot_set_sample(ncdplot* n, uint64_t x, double y){
  if(window_slide_double(n, x)){
    return -1;
  }
  update_sample_double(n, x, y, true);
  if(update_domain_double(n, x)){
    return -1;
  }
  return redraw_plot_double(n);
}

int ncuplot_sample(const ncuplot* n, uint64_t x, uint64_t* y){
  return sample_uint64_t(n, x, y);
}

int ncdplot_sample(const ncdplot* n, uint64_t x, double* y){
  return sample_double(n, x, y);
}

void ncdplot_destroy(ncdplot* n) {
  if(n){
    ncplot_destroy(&n->plot);
    free(n->slots);
    free(n);
  }
}
